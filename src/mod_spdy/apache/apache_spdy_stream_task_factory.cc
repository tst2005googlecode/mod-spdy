// Copyright 2011 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "mod_spdy/apache/apache_spdy_stream_task_factory.h"

// Temporarily define CORE_PRIVATE so we can see the declarations for
// ap_create_conn_config (in http_config.h), ap_process_connection (in
// http_connection.h), and core_module (in http_core.h).
#define CORE_PRIVATE
#include "http_config.h"
#include "http_connection.h"
#include "http_core.h"
#undef CORE_PRIVATE

#include "apr_buckets.h"
#include "apr_network_io.h"
#include "http_log.h"
#include "util_filter.h"

#include "base/basictypes.h"
#include "base/logging.h"
#include "mod_spdy/apache/config_util.h"
#include "mod_spdy/apache/id_pool.h"
#include "mod_spdy/apache/log_message_handler.h"
#include "mod_spdy/apache/master_connection_context.h"
#include "mod_spdy/apache/pool_util.h"
#include "mod_spdy/common/spdy_stream.h"
#include "net/instaweb/util/public/function.h"

namespace mod_spdy {

namespace {

// A task to be returned by ApacheSpdyStreamTaskFactory::NewStreamTask().
class ApacheStreamTask : public net_instaweb::Function {
 public:
  // The task does not take ownership of the arguments.
  ApacheStreamTask(conn_rec* master_connection, SpdyStream* stream);
  virtual ~ApacheStreamTask() {}

 protected:
  // net_instaweb::Function methods:
  virtual void Run();
  virtual void Cancel();

 private:
  SpdyStream* const stream_;
  bool using_ssl_;
  LocalPool local_;
  conn_rec* const slave_connection_;  // allocated in local_.pool()
  apr_socket_t* slave_socket_;
  long master_connection_id_;

  DISALLOW_COPY_AND_ASSIGN(ApacheStreamTask);
};

ApacheStreamTask::ApacheStreamTask(conn_rec* master_connection,
                                   SpdyStream* stream)
    : stream_(stream),
      slave_connection_((conn_rec*)apr_pcalloc(local_.pool(),
                                               sizeof(conn_rec))),
      slave_socket_(NULL),
      master_connection_id_(master_connection->id) {
  // If we are created, the master connection is speaking SPDY, so it
  // should have a master connection context.
  DCHECK(HasMasterConnectionContext(master_connection));
  MasterConnectionContext* master_context =
      GetMasterConnectionContext(master_connection);
  DCHECK(master_context != NULL);
  using_ssl_ = (master_context != NULL) && master_context->is_using_ssl();

  // Initialize what fields of the connection object we can (the rest are
  // zeroed out by apr_pcalloc).  In particular, we should set at least those
  // fields set by core_create_conn() in core.c in Apache.
  // -> id will be set once we are actually running the connection, in
  // ::Run().
  slave_connection_->clogging_input_filters = 0;
  slave_connection_->sbh = NULL;
  // Tie resources for the slave connection to the lifetime of this StreamData
  // object, using our LocalPool.
  slave_connection_->pool = local_.pool();
  slave_connection_->bucket_alloc = apr_bucket_alloc_create(local_.pool());
  slave_connection_->conn_config = ap_create_conn_config(local_.pool());
  slave_connection_->notes = apr_table_make(local_.pool(), 5);
  // Use the same server settings and client address for the slave connection
  // as for the master connection.
  slave_connection_->base_server = master_connection->base_server;
  slave_connection_->local_addr = master_connection->local_addr;
  slave_connection_->local_ip = master_connection->local_ip;
  slave_connection_->remote_addr = master_connection->remote_addr;
  slave_connection_->remote_ip = master_connection->remote_ip;

  // We're supposed to pass a socket object to ap_process_connection below, but
  // there's no meaningful object to pass for this slave connection, because
  // we're not really talking to the network.  Our pre-connection hook will
  // prevent the core filters, which talk to the socket, from being inserted,
  // so they won't notice anyway; nonetheless, we can't pass NULL to
  // ap_process_connection because that can cause some other modules to
  // segfault if they try to muck with the socket's settings.  So, we'll just
  // allocate our own socket object for those modules to mess with.  This is a
  // kludge, but it seems to work.
  const apr_status_t status = apr_socket_create(
      &slave_socket_, APR_INET, SOCK_STREAM, APR_PROTO_TCP, local_.pool());
  DCHECK(status == APR_SUCCESS);
  DCHECK(slave_socket_ != NULL);
}

void ApacheStreamTask::Run() {
  ScopedStreamLogHandler log_handler(slave_connection_, stream_);
  VLOG(3) << "Starting stream task";
  if (!stream_->is_aborted()) {
    // Pick a globally-unique ID for the slave connection; this must be unique
    // at any given time.  Normally the MPM is responsible for assigning these,
    // and each MPM does it differently, so we're cheating in a dangerous way by
    // trying to assign one here.  However, most MPMs seem to do it in a similar
    // way: for non-threaded MPMs (e.g. Prefork, WinNT), the ID is just the
    // child ID, which is a small nonnegative integer (i.e. an array index into
    // the list of active child processes); for threaded MPMs (e.g. Worker,
    // Event) the ID is typically ((child_index * thread_limit) + thread_index),
    // which will again be a positive integer, most likely (but not necessarily,
    // if thread_limit is set absurdly high) smallish.
    //
    // Therefore, the approach that we take is to concatenate the Apache
    // connection ID for the master connection with a small integer from IDPool
    // that's unique within the process, and, to avoid conflicts with
    // MPM-assigned connection IDs, we make our slave connection ID negative.
    // We only have so many bits to work with
    // (especially if long is only four bytes instead of eight), so we could
    // potentially run into trouble if the master connection ID gets very large
    // or we have too many active tasks simultaneously (i.e. more than 2^16).
    // So, this approach definitely isn't any kind of robust; but it will
    // probably usually work. It would, of course, be great to replace this
    // with a better strategy, if we find one.
    //
    // TODO(mdsteele): We could also consider using an #if here to widen the
    //   masks and the shift distance on systems where sizeof(long)==8.
    //   We might as well use those extra bits if we have them.
    COMPILE_ASSERT(sizeof(long) >= 4, long_is_at_least_32_bits);
    const uint16 in_process_id = IdPool::Instance()->Alloc();
    const long slave_connection_id =
        -(((master_connection_id_ & 0x7fffL) << 16) | in_process_id);
    slave_connection_->id = slave_connection_id;

    // In our context object for this connection, mark this connection as being
    // a slave.  Our pre-connection and process-connection hooks will notice
    // this, and act accordingly, when they are called for the slave
    // connection.
    CreateSlaveConnectionContext(slave_connection_, using_ssl_, stream_);

    // Normally, the core pre-connection hook sets the core module's connection
    // context to the socket passed to ap_process_connection; certain other
    // modules, such as mod_reqtimeout, read the core module's connection
    // context directly so as to read this socket's settings.  However, we
    // purposely don't allow the core pre-connection hook to run, because we
    // don't want the core connection filters to be inserted.  So, to avoid
    // breaking other modules, we take it upon oursevles to set the core
    // module's connection context to the socket we are passing to
    // ap_process_connection.  This is ugly, but seems to work.
    ap_set_module_config(slave_connection_->conn_config,
                         &core_module, slave_socket_);

    // Invoke Apache's usual processing pipeline.  This will block until the
    // connection is complete.
    ap_process_connection(slave_connection_, slave_socket_);

    IdPool::Instance()->Free(in_process_id);
  }
  VLOG(3) << "Finishing stream task";
}

void ApacheStreamTask::Cancel() {
  if (VLOG_IS_ON(3)) {
    ScopedStreamLogHandler log_handler(slave_connection_, stream_);
    VLOG(3) << "Cancelling stream task";
  }
}

}  // namespace

ApacheSpdyStreamTaskFactory::ApacheSpdyStreamTaskFactory(conn_rec* connection)
    : connection_(connection) {}

ApacheSpdyStreamTaskFactory::~ApacheSpdyStreamTaskFactory() {}

net_instaweb::Function* ApacheSpdyStreamTaskFactory::NewStreamTask(
    SpdyStream* stream) {
  return new ApacheStreamTask(connection_, stream);
}

}  // namespace mod_spdy
