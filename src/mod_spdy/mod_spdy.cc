// Copyright 2010 Google Inc.
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

// References to "TAMB" below refer to _The Apache Modules Book_ by Nick Kew
// (ISBN: 0-13-240967-4).

#include "mod_spdy/mod_spdy.h"

#include <algorithm>  // for std::min

#include "httpd.h"
#include "http_connection.h"
#include "http_config.h"
#include "http_log.h"
#include "http_protocol.h"
#include "http_request.h"
#include "apr_optional.h"
#include "apr_optional_hooks.h"
#include "apr_tables.h"

#include "base/memory/scoped_ptr.h"
#include "mod_spdy/apache/apache_spdy_session_io.h"
#include "mod_spdy/apache/apache_spdy_stream_task_factory.h"
#include "mod_spdy/apache/config_commands.h"
#include "mod_spdy/apache/config_util.h"
#include "mod_spdy/apache/filters/http_to_spdy_filter.h"
#include "mod_spdy/apache/filters/spdy_to_http_filter.h"
#include "mod_spdy/apache/log_message_handler.h"
#include "mod_spdy/apache/pool_util.h"
#include "mod_spdy/common/connection_context.h"
#include "mod_spdy/common/executor.h"
#include "mod_spdy/common/protocol_util.h"
#include "mod_spdy/common/spdy_server_config.h"
#include "mod_spdy/common/spdy_session.h"
#include "mod_spdy/common/thread_pool.h"

extern "C" {
  // Declaring modified mod_ssl's optional hooks here (so that we don't need to
  // #include "mod_ssl.h").
  APR_DECLARE_OPTIONAL_FN(int, ssl_engine_disable, (conn_rec *));
  APR_DECLARE_OPTIONAL_FN(int, ssl_is_https, (conn_rec *));
  APR_DECLARE_EXTERNAL_HOOK(
      ssl, AP, int, npn_advertise_protos_hook,
      (conn_rec* connection, apr_array_header_t* protos));
  APR_DECLARE_EXTERNAL_HOOK(
      ssl, AP, int, npn_proto_negotiated_hook,
      (conn_rec* connection, char* proto_name, apr_size_t proto_name_len));
}  // extern "C"

namespace {

// For now, we only support SPDY version 2.  Note that if we ever decide to
// support multiple SPDY versions simultaneously, we will need to make some
// structural changes to the code.
// TODO(mdsteele): Pretty soon we will probably need to support SPDY v3.
const int kSpdyVersionNumber = 2;
const char* const kSpdyVersionNumberString = "2";
const char* const kSpdyVersionEnvironmentVariable = "SPDY_VERSION";
const char* const kSpdyProtocolName = "spdy/2";
const char* const kHttpProtocolName = "http/1.1";

// These global variables store the filter handles for our filters.  Normally,
// global variables would be very dangerous in a concurrent environment like
// Apache, but these ones are okay because they are assigned just once, at
// start-up (during which Apache is running single-threaded; see TAMB 2.2.1),
// and are read-only thereafter.
ap_filter_rec_t* gAntiChunkingFilterHandle = NULL;
ap_filter_rec_t* gHttpToSpdyFilterHandle = NULL;
ap_filter_rec_t* gSpdyToHttpFilterHandle = NULL;

// These global variables store pointers to "optional functions" defined in
// mod_ssl.  See TAMB 10.1.2 for more about optional functions.  These, too,
// are assigned just once, at start-up.
int (*gDisableSslForConnection)(conn_rec*) = NULL;
int (*gIsUsingSslForConnection)(conn_rec*) = NULL;

// A process-global thread pool for processing SPDY streams concurrently.  This
// is initialized once in *each child process* by our child-init hook.  Note
// that in a non-threaded MPM (e.g. Prefork), this thread pool will be used by
// just one SPDY connection at a time, but in a threaded MPM (e.g. Worker) it
// will shared by several SPDY connections at once.  That's okay though,
// because ThreadPool objects are thread-safe.  Users just have to make sure
// that they configure SpdyMaxThreadsPerProcess depending on the MPM.
mod_spdy::ThreadPool* gPerProcessThreadPool = NULL;

// Optional function provided by mod_spdy.  Return zero if the connection is
// not using SPDY, otherwise return the SPDY version number in use.  Note that
// unlike our private functions, we use Apache C naming conventions for this
// function because we export it to other modules.
int spdy_get_version(conn_rec* connection) {
  const mod_spdy::ConnectionContext* context =
      mod_spdy::GetConnectionContext(connection);
  if (context != NULL && context->is_using_spdy()) {
    COMPILE_ASSERT(kSpdyVersionNumber != 0, version_number_is_nonzero);
    return kSpdyVersionNumber;
  }
  return 0;
}

// See TAMB 8.4.2
apr_status_t SpdyToHttpFilter(ap_filter_t* filter,
                              apr_bucket_brigade* brigade,
                              ap_input_mode_t mode,
                              apr_read_type_e block,
                              apr_off_t readbytes) {
  mod_spdy::SpdyToHttpFilter* spdy_to_http_filter =
      static_cast<mod_spdy::SpdyToHttpFilter*>(filter->ctx);
  return spdy_to_http_filter->Read(filter, brigade, mode, block, readbytes);
}

apr_status_t AntiChunkingFilter(ap_filter_t* filter,
                                apr_bucket_brigade* input_brigade) {
  // Make sure no one is already trying to chunk the data in this request.
  request_rec* request = filter->r;
  if (request->chunked != 0) {
    LOG(DFATAL) << "request->chunked == " << request->chunked
                << " in request " << request->the_request;
  }
  const char* transfer_encoding =
      apr_table_get(request->headers_out, mod_spdy::http::kTransferEncoding);
  if (transfer_encoding != NULL) {
    LOG(DFATAL) << "transfer_encoding == \"" << transfer_encoding << "\""
                << " in request " << request->the_request;
  }

  // Setting the Transfer-Encoding header to "chunked" here will trick the core
  // HTTP_HEADER filter into not inserting the CHUNK filter.  We later remove
  // this header in our http-to-spdy filter.  It's a gross hack, but it seems
  // to work, and is much simpler than allowing the data to be chunked and then
  // having to de-chunk it ourselves.
  apr_table_setn(request->headers_out, mod_spdy::http::kTransferEncoding,
                 mod_spdy::http::kChunked);

  // This filter only needs to run once, so now that it has run, remove it.
  ap_remove_output_filter(filter);
  return ap_pass_brigade(filter->next, input_brigade);
}

// See TAMB 8.4.1
apr_status_t HttpToSpdyFilter(ap_filter_t* filter,
                              apr_bucket_brigade* input_brigade) {
  // First, we need to do a couple things that are relevant to the details of
  // the anti-chunking filter.  We'll do them here rather than in the
  // HttpToSpdyFilter class so that we can see them right next to the
  // anti-chunking filter.

  // Make sure nothing unexpected has happened to the transfer encoding between
  // here and our anti-chunking filter.
  request_rec* request = filter->r;
  if (request->chunked != 0) {
    LOG(DFATAL) << "request->chunked == " << request->chunked
                << " in request " << request->the_request;
  }
  const char* transfer_encoding =
      apr_table_get(filter->r->headers_out, mod_spdy::http::kTransferEncoding);
  if (transfer_encoding != NULL && strcmp(transfer_encoding, "chunked")) {
    LOG(DFATAL) << "transfer_encoding == \"" << transfer_encoding << "\""
                << " in request " << request->the_request;
  }
  // Remove the transfer-encoding header so that it does not appear in our SPDY
  // headers.
  apr_table_unset(request->headers_out, mod_spdy::http::kTransferEncoding);

  // Okay, now that that's done, let's focus on translating HTTP to SPDY.
  mod_spdy::HttpToSpdyFilter* http_to_spdy_filter =
      static_cast<mod_spdy::HttpToSpdyFilter*>(filter->ctx);
  return http_to_spdy_filter->Write(filter, input_brigade);
}

// Called on server startup, after all modules have loaded.
void RetrieveOptionalFunctions() {
  gDisableSslForConnection = APR_RETRIEVE_OPTIONAL_FN(ssl_engine_disable);
  gIsUsingSslForConnection = APR_RETRIEVE_OPTIONAL_FN(ssl_is_https);
  // If mod_ssl isn't installed, we'll get back NULL for these functions.  Our
  // other hook functions will fail gracefully (i.e. do nothing) if these
  // functions are NULL, but if the user installed mod_spdy without mod_ssl and
  // expected it to do anything, we should warn them otherwise.
  //
  // Note: Alternatively, it may be that there's no mod_ssl, but mod_spdy has
  // been configured to assume SPDY for non-SSL connections, in which case this
  // warning is untrue.  But there's no easy way to check the server config
  // from here, and normal users should never use that config option anyway
  // (it's for debugging), so I don't think the spurious warning is a big deal.
  if (gDisableSslForConnection == NULL &&
      gIsUsingSslForConnection == NULL) {
    LOG(WARNING) << "It seems that mod_spdy is installed but mod_ssl isn't.  "
                 << "Without SSL, the server cannot ever use SPDY.";
  }
  // Whether or not mod_ssl is installed, either both functions should be
  // non-NULL or both functions should be NULL.  Otherwise, something is wrong
  // (like, maybe some kind of bizarre mutant mod_ssl is installed) and
  // mod_spdy probably won't work correctly.
  if ((gDisableSslForConnection == NULL) ^
      (gIsUsingSslForConnection == NULL)) {
    LOG(DFATAL) << "Some, but not all, of mod_ssl's optional functions are "
                << "available.  What's going on?";
  }
}

// Called after configuration has completed.
int PostConfig(apr_pool_t* pconf, apr_pool_t* plog, apr_pool_t* ptemp,
               server_rec* server_list) {
  mod_spdy::ScopedServerLogHandler log_handler(server_list);

  // Check if any of the virtual hosts have mod_spdy enabled.
  bool any_enabled = false;
  for (server_rec* server = server_list; server != NULL;
       server = server->next) {
    if (mod_spdy::GetServerConfig(server)->spdy_enabled()) {
      any_enabled = true;
      break;
    }
  }

  // Log a message indicating whether mod_spdy is enabled or not.  It's all too
  // easy to install mod_spdy and forget to turn it on, so this may be helpful
  // for debugging server behavior.
  if (!any_enabled) {
    LOG(WARNING) << "mod_spdy is installed, but has not been enabled in the "
                 << "Apache config. SPDY will not be used by this server.  "
                 << "See http://code.google.com/p/mod-spdy/wiki/ConfigOptions "
                 << "for how to enable.";
  }

  return OK;
}

// Called exactly once for each child process, before that process starts
// spawning worker threads.
void ChildInit(apr_pool_t* pool, server_rec* server_list) {
  mod_spdy::ScopedServerLogHandler log_handler(server_list);

  // Check whether mod_spdy is enabled for any server_rec in the list, and
  // determine the most verbose log level of any server in the list.
  bool spdy_enabled = false;
  int max_apache_log_level = APLOG_EMERG;  // the least verbose log level
  COMPILE_ASSERT(APLOG_INFO > APLOG_ERR, bigger_number_means_more_verbose);
  for (server_rec* server = server_list; server != NULL;
       server = server->next) {
    spdy_enabled |= mod_spdy::GetServerConfig(server)->spdy_enabled();
    if (server->loglevel > max_apache_log_level) {
      max_apache_log_level = server->loglevel;
    }
  }

  // There are a couple config options we need to check (vlog_level and
  // max_threads_per_process) that are only settable at the top level of the
  // config, so it doesn't matter which server in the list we read them from.
  const mod_spdy::SpdyServerConfig* top_level_config =
      mod_spdy::GetServerConfig(server_list);

  // We set mod_spdy's global logging level to that of the most verbose server
  // in the list.  The scoped logging handlers we establish will sometimes
  // restrict things further, if they are for a less verbose virtual host.
  mod_spdy::SetLoggingLevel(max_apache_log_level,
                            top_level_config->vlog_level());

  // If mod_spdy is not enabled on any server_rec, don't do any other setup.
  if (!spdy_enabled) {
    return;
  }

  // Create the per-process thread pool.
  const int max_threads = top_level_config->max_threads_per_process();
  const int min_threads =
      std::min(max_threads, top_level_config->min_threads_per_process());
  scoped_ptr<mod_spdy::ThreadPool> thread_pool(
      new mod_spdy::ThreadPool(min_threads, max_threads));
  if (thread_pool->Start()) {
    gPerProcessThreadPool = thread_pool.release();
    mod_spdy::PoolRegisterDelete(pool, gPerProcessThreadPool);
  } else {
    LOG(DFATAL) << "Could not create mod_spdy thread pool; "
                << "mod_spdy will not function.";
  }
}

// A pre-connection hook, to be run _before_ mod_ssl's pre-connection hook.
// Disables mod_ssl for our slave connections.
int DisableSslForSlaves(conn_rec* connection, void* csd) {
  mod_spdy::ScopedConnectionLogHandler log_handler(connection);

  const mod_spdy::ConnectionContext* context =
      mod_spdy::GetConnectionContext(connection);

  // For master connections, the context object won't have been created yet (it
  // gets created in PreConnection).
  if (context == NULL) {
    return DECLINED;
  }

  // If the context has already been created, this must be a slave connection
  // (and mod_spdy must be enabled).
  DCHECK(context->is_slave());
  DCHECK(mod_spdy::GetServerConfig(connection)->spdy_enabled());

  // Disable mod_ssl for the slave connection so it doesn't get in our way.
  if (gDisableSslForConnection == NULL ||
      gDisableSslForConnection(connection) == 0) {
    // Hmm, mod_ssl either isn't installed or isn't enabled.  That should be
    // impossible (we wouldn't _have_ a slave connection without having SSL for
    // the master connection), unless we're configured to assume SPDY for
    // non-SSL connections.  Let's check if that's the case, and LOG(DFATAL) if
    // it's not.
    if (!mod_spdy::GetServerConfig(connection)->use_even_without_ssl()) {
      LOG(DFATAL) << "mod_ssl missing for slave connection";
    }
  }
  return OK;
}

// A pre-connection hook, to be run _after_ mod_ssl's pre-connection hook, but
// just _before_ the core pre-connection hook.  For master connections, this
// checks if SSL is active; for slave connections, this adds our
// connection-level filters and prevents core filters from being inserted.
int PreConnection(conn_rec* connection, void* csd) {
  mod_spdy::ScopedConnectionLogHandler log_handler(connection);

  mod_spdy::ConnectionContext* context =
      mod_spdy::GetConnectionContext(connection);

  // If the connection context has not yet been created, this is a "real"
  // connection (not one of our slave connections).
  if (context == NULL) {
    // If mod_spdy is disabled on this server, don't allocate our context
    // object.
    const mod_spdy::SpdyServerConfig* config =
        mod_spdy::GetServerConfig(connection);
    if (!config->spdy_enabled()) {
      return DECLINED;
    }

    bool assume_spdy = false;

    // Check if this connection is over SSL; if not, we can't do NPN, so we
    // definitely won't be using SPDY (unless we're configured to assume SPDY
    // for non-SSL connections).
    const bool using_ssl = (gIsUsingSslForConnection != NULL &&
                            gIsUsingSslForConnection(connection) != 0);
    if (!using_ssl) {
      // This is not an SSL connection, so we can't talk SPDY on it _unless_ we
      // have opted to assume SPDY over non-SSL connections (presumably for
      // debugging purposes; this would normally break browsers).
      if (config->use_even_without_ssl()) {
        assume_spdy = true;
      } else {
        return DECLINED;
      }
    }

    // Okay, we've got a real connection over SSL, so we'll be negotiating with
    // the client to see if we can use SPDY for this connection.  Create our
    // connection context object to keep track of the negotiation.
    context = mod_spdy::CreateMasterConnectionContext(connection, using_ssl);
    // If we're assuming SPDY, we don't even need to do the negotiation.
    if (assume_spdy) {
      context->set_assume_spdy(true);
    }
    return OK;
  }
  // If the context has already been created, this is a slave connection.
  else {
    DCHECK(context->is_slave());
    DCHECK(mod_spdy::GetServerConfig(connection)->spdy_enabled());

    // Instantiate and add our SPDY-to-HTTP filter for the slave connection.
    // This is an Apache connection-level filter, so we add it here.  The
    // corresponding HTTP-to-SPDY filter is request-level, so we add that one
    // in InsertRequestFilters().
    mod_spdy::SpdyToHttpFilter* spdy_to_http_filter =
        new mod_spdy::SpdyToHttpFilter(context->slave_stream());
    mod_spdy::PoolRegisterDelete(connection->pool, spdy_to_http_filter);
    ap_add_input_filter_handle(
        gSpdyToHttpFilterHandle,  // filter handle
        spdy_to_http_filter,      // context (any void* we want)
        NULL,                     // request object
        connection);              // connection object

    // Prevent core pre-connection hooks from running (thus preventing core
    // filters from being inserted).
    return DONE;
  }
}

// Called to see if we want to take care of processing this connection -- if
// so, we do so and return OK, otherwise we return DECLINED.  For slave
// connections, we want to return DECLINED.  For "real" connections, we need to
// determine if they are using SPDY; if not we returned DECLINED, but if so we
// process this as a master SPDY connection and then return OK.
int ProcessConnection(conn_rec* connection) {
  mod_spdy::ScopedConnectionLogHandler log_handler(connection);

  // If mod_spdy is disabled on this server, don't use SPDY.
  const mod_spdy::SpdyServerConfig* config =
      mod_spdy::GetServerConfig(connection);
  if (!config->spdy_enabled()) {
    return DECLINED;
  }

  // We do not want to attach to non-inbound connections (e.g. connections
  // created by mod_proxy).  Non-inbound connections do not get a scoreboard
  // hook, so we abort if the connection doesn't have the scoreboard hook.  See
  // http://mail-archives.apache.org/mod_mbox/httpd-dev/201008.mbox/%3C99EA83DCDE961346AFA9B5EC33FEC08B047FDC26@VF-MBX11.internal.vodafone.com%3E
  // for more details.
  if (connection->sbh == NULL) {
    return DECLINED;
  }

  // Our connection context object will have been created by now, unless our
  // pre-connection hook saw that this was a non-SSL connection, in which case
  // we won't be using SPDY so we can stop now.
  const mod_spdy::ConnectionContext* context =
      mod_spdy::GetConnectionContext(connection);
  if (context == NULL) {
    return DECLINED;
  }

  // If this is one of our slave connections (rather than a "real" connection),
  // then we don't want to deal with it here -- instead we will let Apache
  // treat it like a regular HTTP connection.
  if (context->is_slave()) {
    return DECLINED;
  }

  // In the unlikely event that we failed to create our per-process thread
  // pool, we're not going to be able to operate.
  if (gPerProcessThreadPool == NULL) {
    return DECLINED;
  }

  // Unless we're simply assuming SPDY for this connection, we need to do NPN
  // to decide whether to use SPDY or not.
  if (!context->is_assuming_spdy()) {
    // We need to pull some data through mod_ssl in order to force the SSL
    // handshake, and hence NPN, to take place.  To that end, perform a small
    // SPECULATIVE read (and then throw away whatever data we got).
    apr_bucket_brigade* temp_brigade =
        apr_brigade_create(connection->pool, connection->bucket_alloc);
    const apr_status_t status =
        ap_get_brigade(connection->input_filters, temp_brigade,
                       AP_MODE_SPECULATIVE, APR_BLOCK_READ, 1);
    apr_brigade_destroy(temp_brigade);

    // If we were unable to pull any data through, give up and return DECLINED.
    if (status != APR_SUCCESS) {
      // Depending on exactly what went wrong, we may want to log something
      // before returning DECLINED.
      if (APR_STATUS_IS_EOF(status)) {
        // EOF errors are to be expected sometimes (e.g. if the connection was
        // closed), and we should just quietly give up.  No need to log in this
        // case.
      } else if (APR_STATUS_IS_TIMEUP(status)) {
        // TIMEUP errors also seem to happen occasionally.  I think we should
        // also give up in this case, but I'm not sure yet; for now let's VLOG
        // when it happens, to help with debugging [mdsteele].
        VLOG(1) << "Speculative read returned TIMEUP.";
      } else {
        // Any other error might be a real problem, so let's log it.
        LOG(ERROR) << "Speculative read failed with status " << status << ": "
                   << mod_spdy::AprStatusString(status);
      }
      return DECLINED;
    }

    // If we did pull some data through, then NPN should have happened and our
    // OnNextProtocolNegotiated() hook should have been called by now.  If NPN
    // hasn't happened, it's probably because we're using an old version of
    // mod_ssl that doesn't support NPN, in which case we should probably warn
    // the user that mod_spdy isn't going to work.
    if (context->npn_state() == mod_spdy::ConnectionContext::NOT_DONE_YET) {
      LOG(WARNING)
          << "NPN didn't happen during SSL handshake.  You're probably using "
          << "a version of mod_ssl that doesn't support NPN. Without NPN "
          << "support, the server cannot use SPDY. See "
          << "http://code.google.com/p/mod-spdy/wiki/GettingStarted for more "
          << "information on installing a version of mod_spdy with NPN "
          << "support.";
    }
  }

  // If NPN didn't choose SPDY, then don't use SPDY.
  if (!context->is_using_spdy()) {
    return DECLINED;
  }

  VLOG(1) << "Starting SPDY session";

  // At this point, we and the client have agreed to use SPDY (either that, or
  // we've been configured to use SPDY regardless of what the client says), so
  // process this as a SPDY master connection.
  mod_spdy::ApacheSpdySessionIO session_io(connection);
  mod_spdy::ApacheSpdyStreamTaskFactory task_factory(connection);
  scoped_ptr<mod_spdy::Executor> executor(
      gPerProcessThreadPool->NewExecutor());
  mod_spdy::SpdySession spdy_session(
      config, &session_io, &task_factory, executor.get());
  // This call will block until the session has closed down.
  spdy_session.Run();

  VLOG(1) << "Terminating SPDY session";

  // Return OK to tell Apache that we handled this connection.
  return OK;
}

// Called by mod_ssl when it needs to decide what protocols to advertise to the
// client during Next Protocol Negotiation (NPN).
int AdvertiseSpdy(conn_rec* connection, apr_array_header_t* protos) {
  // If mod_spdy is disabled on this server, then we shouldn't advertise SPDY
  // to the client.
  if (!mod_spdy::GetServerConfig(connection)->spdy_enabled()) {
    return DECLINED;
  }

  // Advertise SPDY to the client.
  // TODO(mdsteele): Pretty soon we will probably need to support SPDY v3.  If
  //   we want to support both v2 and v3, we need to advertise both of them
  //   here; the one we prefer (presumably v3) should be pushed first.
  APR_ARRAY_PUSH(protos, const char*) = kSpdyProtocolName;
  return OK;
}

// Called by mod_ssl (along with the AdvertiseSpdy function) when it needs to
// decide what protocols to advertise to the client during Next Protocol
// Negotiation (NPN).  These two functions are separate so that AdvertiseSpdy
// can run early in the hook order, and AdvertiseHttp can run late.
int AdvertiseHttp(conn_rec* connection, apr_array_header_t* protos) {
  // If mod_spdy is disabled on this server, don't do anything.
  if (!mod_spdy::GetServerConfig(connection)->spdy_enabled()) {
    return DECLINED;
  }

  // Apache definitely supports HTTP/1.1, and so it ought to advertise it
  // during NPN.  However, the Apache core HTTP module doesn't yet know about
  // this hook, so we advertise HTTP/1.1 for them.  But to be future-proof, we
  // don't add "http/1.1" to the list if it's already there.
  for (int i = 0; i < protos->nelts; ++i) {
    if (!strcmp(APR_ARRAY_IDX(protos, i, const char*), kHttpProtocolName)) {
      return DECLINED;
    }
  }

  // No one's advertised HTTP/1.1 yet, so let's do it now.
  APR_ARRAY_PUSH(protos, const char*) = kHttpProtocolName;
  return OK;
}

// Called by mod_ssl after Next Protocol Negotiation (NPN) has completed,
// informing us which protocol was chosen by the client.
int OnNextProtocolNegotiated(conn_rec* connection, char* proto_name,
                             apr_size_t proto_name_len) {
  mod_spdy::ScopedConnectionLogHandler log_handler(connection);

  // If mod_spdy is disabled on this server, then ignore the results of NPN.
  if (!mod_spdy::GetServerConfig(connection)->spdy_enabled()) {
    return DECLINED;
  }

  mod_spdy::ConnectionContext* context =
      mod_spdy::GetConnectionContext(connection);

  // Given that mod_spdy is enabled, our context object should have already
  // been created in our pre-connection hook, unless this is a non-SSL
  // connection.  But if it's a non-SSL connection, then NPN shouldn't be
  // happening, and this hook shouldn't be getting called!  So, let's
  // LOG(DFATAL) if context is NULL here.
  if (context == NULL) {
    LOG(DFATAL) << "NPN happened, but there is no connection context.";
    return DECLINED;
  }

  // We disable mod_ssl for slave connections, so NPN shouldn't be happening
  // unless this is a non-slave connection.
  if (context->is_slave()) {
    LOG(DFATAL) << "mod_ssl was aparently not disabled for slave connection";
    return DECLINED;
  }

  // NPN should happen only once, so npn_state should still be NOT_DONE_YET.
  if (context->npn_state() != mod_spdy::ConnectionContext::NOT_DONE_YET) {
    LOG(DFATAL) << "NPN happened twice.";
    return DECLINED;
  }

  // If the client chose the SPDY version that we advertised, then mark this
  // connection as using SPDY.
  if (proto_name_len == strlen(kSpdyProtocolName) &&
      !strncmp(kSpdyProtocolName, proto_name, proto_name_len)) {
    context->set_npn_state(mod_spdy::ConnectionContext::USING_SPDY);
  }
  // Otherwise, explicitly mark this connection as not using SPDY.
  else {
    context->set_npn_state(mod_spdy::ConnectionContext::NOT_USING_SPDY);
  }
  return OK;
}

// Invoked once per HTTP request, when the request object is created.  We use
// this to insert our protocol-level output filter.
int InsertProtocolFilters(request_rec* request) {
  conn_rec* connection = request->connection;
  mod_spdy::ScopedConnectionLogHandler log_handler(connection);

  // If mod_spdy is disabled on this server, then don't insert any filters.
  if (!mod_spdy::GetServerConfig(connection)->spdy_enabled()) {
    return DECLINED;
  }

  const mod_spdy::ConnectionContext* context =
      mod_spdy::GetConnectionContext(connection);

  // Given that mod_spdy is enabled, our context object should be present by
  // now (having been created in our pre-connection hook) unless this is a
  // non-SSL connection, in which case we definitely aren't using SPDY.
  if (context == NULL) {
    return DECLINED;
  }

  // If this isn't one of our slave connections, don't insert any filters.
  if (!context->is_slave()) {
    return DECLINED;
  }

  // Instantiate and add our HTTP-to-SPDY filter for the slave connection.
  // This is an Apache protocol-level filter (specifically,
  // AP_FTYPE_TRANSCODE), so we add it here.  The corresponding SPDY-to-HTTP
  // filter is connection-level, so we add that one in PreConnection().
  mod_spdy::HttpToSpdyFilter* http_to_spdy_filter =
      new mod_spdy::HttpToSpdyFilter(context->slave_stream());
  PoolRegisterDelete(request->pool, http_to_spdy_filter);

  ap_add_output_filter_handle(
      gHttpToSpdyFilterHandle,    // filter handle
      http_to_spdy_filter,        // context (any void* we want)
      request,                    // request object
      connection);                // connection object

  return OK;
}

// Invoked once (or possibly twice) per HTTP request.  However, in cases where
// it is called twice, content-level filters (that is, those with filter type <
// AP_FTYPE_PROTOCOL) are discarded before the second invocation.  Since we use
// this hook only to insert such filters, that is fine.
void InsertContentFilters(request_rec* request) {
  conn_rec* connection = request->connection;
  mod_spdy::ScopedConnectionLogHandler log_handler(connection);

  // If mod_spdy is disabled on this server, then don't insert any filters.
  if (!mod_spdy::GetServerConfig(connection)->spdy_enabled()) {
    return;
  }

  // Same check as in InsertProtocolFilters above: proceed only if our context
  // is present and this is a slave connection.
  const mod_spdy::ConnectionContext* context =
      mod_spdy::GetConnectionContext(connection);
  if (context == NULL || !context->is_slave()) {
    return;
  }

  // Instantiate our anti-chunking filter for the slave connection.  This is an
  // Apache content-level filter, so we add it here.
  ap_add_output_filter_handle(
      gAntiChunkingFilterHandle,  // filter handle
      NULL,                       // context (any void* we want)
      request,                    // request object
      connection);                // connection object
}

int SetUpSubprocessEnv(request_rec* request) {
  conn_rec* connection = request->connection;
  mod_spdy::ScopedConnectionLogHandler log_handler(connection);

  // If mod_spdy is disabled on this server, then don't do anything.
  if (!mod_spdy::GetServerConfig(connection)->spdy_enabled()) {
    return DECLINED;
  }

  // Don't do anything unless this is a slave connection.
  const mod_spdy::ConnectionContext* context =
      mod_spdy::GetConnectionContext(connection);
  if (context == NULL || !context->is_slave()) {
    return DECLINED;
  }

  // For the benefit of CGI scripts, which have no way of calling
  // spdy_get_version(), set an environment variable indicating that this
  // request is over SPDY (and what SPDY version is being used), allowing them
  // to optimize the response for SPDY.
  // See http://code.google.com/p/mod-spdy/issues/detail?id=27 for details.
  apr_table_setn(request->subprocess_env, kSpdyVersionEnvironmentVariable,
                 kSpdyVersionNumberString);

  // Normally, mod_ssl sets the HTTPS environment variable to "on" for requests
  // served over SSL.  We turn mod_ssl off for our slave connections, but those
  // requests _are_ (usually) being served over SSL (via the master
  // connection), so we set the variable ourselves if we are in fact using SSL.
  // See http://code.google.com/p/mod-spdy/issues/detail?id=32 for details.
  if (context->is_using_ssl()) {
    apr_table_setn(request->subprocess_env, "HTTPS", "on");
  }

  return OK;
}

// Called when the module is loaded to register all of our hook functions.
void RegisterHooks(apr_pool_t* pool) {
  mod_spdy::InstallLogMessageHandler(pool);

  static const char* const modules_core[] = {"core.c", NULL};
  static const char* const modules_mod_ssl[] = {"mod_ssl.c", NULL};

  // Register a hook to be called after all modules have been loaded, so we can
  // retrieve optional functions from mod_ssl.
  ap_hook_optional_fn_retrieve(
      RetrieveOptionalFunctions,  // hook function to be called
      NULL,                       // predecessors
      NULL,                       // successors
      APR_HOOK_MIDDLE);           // position

  // Register a hook to be called after configuration has completed.  We use
  // this hook to log whether or not mod_spdy is enabled on this server.
  ap_hook_post_config(PostConfig, NULL, NULL, APR_HOOK_MIDDLE);

  // Register a hook to be called once for each child process spawned by
  // Apache, before the MPM starts spawning worker threads.  We use this hook
  // to initialize our per-process thread pool.
  ap_hook_child_init(ChildInit, NULL, NULL, APR_HOOK_MIDDLE);

  // Register a pre-connection hook to turn off mod_ssl for our slave
  // connections.  This must run before mod_ssl's pre-connection hook, so that
  // we can disable mod_ssl before it inserts its filters, so we name mod_ssl
  // as an explicit successor.
  ap_hook_pre_connection(
      DisableSslForSlaves,        // hook function to be called
      NULL,                       // predecessors
      modules_mod_ssl,            // successors
      APR_HOOK_FIRST);            // position

  // Register our pre-connection hook, which will be called shortly before our
  // process-connection hook.  The hooking order is very important here.  In
  // particular:
  //   * We must run before the core pre-connection hook, so that we can return
  //     DONE and stop the core filters from being inserted.  Thus, we name
  //     core.c as a successor.
  //   * We should run after almost all other modules (except core.c) so that
  //     our returning DONE doesn't prevent other modules from working.  Thus,
  //     we use APR_HOOK_LAST for our position argument.
  //   * In particular, we MUST run after mod_ssl's pre-connection hook, so
  //     that we can ask mod_ssl if this connection is using SSL.  Thus, we
  //     name mod_ssl.c as a predecessor.  This is redundant, since mod_ssl's
  //     pre-connection hook uses APR_HOOK_MIDDLE, but it's good to be sure.
  // For more about controlling hook order, see TAMB 10.2.2 or
  // http://httpd.apache.org/docs/trunk/developer/hooks.html#hooking-order
  ap_hook_pre_connection(
      PreConnection,              // hook function to be called
      modules_mod_ssl,            // predecessors
      modules_core,               // successors
      APR_HOOK_LAST);             // position

  // Register our process-connection hook, which will handle SPDY connections.
  // The first process-connection hook in the chain to return OK gets to be in
  // charge of handling the connection from start to finish, so we put
  // ourselves in APR_HOOK_FIRST so we can get an early look at the connection.
  // If it turns out not to be a SPDY connection, we'll get out of the way and
  // let other modules deal with it.
  ap_hook_process_connection(ProcessConnection, NULL, NULL, APR_HOOK_FIRST);

  // Register three different hooks to insert our request filters.  Now, this
  // is a little tricky.  We have two output filters, which must be inserted
  // (exactly once) for all requests whether they error or not: one with type
  // AP_FTYPE_TRANSCODE and one with type AP_FTYPE_PROTOCOL-1.  Why not insert
  // both from just one hook?  Because there's no one hook that's appropriate.
  // This is roughly how these three hooks are used by Apache:
  //   * The request is read, the request object is created, and the
  //     post_read_request hook is called.
  //   * Authentication is checked; if a 401 is generated, then all request
  //     filters with type < AP_FTYPE_PROTOCOL are removed, the
  //     insert_error_filter hook is called, and the error response is sent.
  //   * Otherwise, the insert_filter hook is called, and the request is
  //     processed.
  //   * If the request results in an error (e.g. 500), then all request
  //     filters with type < AP_FTYPE_PROTOCOL are removed, the
  //     insert_error_filter hook is called, and the error response is sent.
  // Our filter with type AP_FTYPE_PROTOCOL-1 will get removed in either error
  // case, so we must add it back in the insert_error_filter hook.  We must
  // also add it in the insert_filter hook, in the case that no error occurs;
  // so we can just use the same function for both of these hooks.  To avoid
  // our AP_FTYPE_TRANSCODE filter from getting added twice in the case of
  // e.g. 500 errors, we add it not in that function, but from the
  // post_read_request hook.  The result is relatively clean: one function for
  // adding each of our two output filters, one of which is called by one hook
  // and one of which is called by two.
  // See also http://code.google.com/p/mod-spdy/issues/detail?id=24
  ap_hook_post_read_request(
      InsertProtocolFilters, NULL, NULL, APR_HOOK_MIDDLE);
  ap_hook_insert_filter(
      InsertContentFilters, NULL, NULL, APR_HOOK_MIDDLE);
  ap_hook_insert_error_filter(
      InsertContentFilters, NULL, NULL, APR_HOOK_MIDDLE);

  // For the benefit of e.g. PHP/CGI scripts, we need to set various subprocess
  // environment variables for each request served via SPDY.  Register a hook
  // to do so; we use the fixup hook for this because that's the same hook that
  // mod_ssl uses for setting its subprocess environment variables.
  ap_hook_fixups(SetUpSubprocessEnv, NULL, NULL, APR_HOOK_MIDDLE);

  // Register a hook with mod_ssl to be called when deciding what protocols to
  // advertise during Next Protocol Negotiatiation (NPN); we'll use this
  // opportunity to advertise that we support SPDY.  This hook is declared in
  // mod_ssl.h, for appropriately-patched versions of mod_ssl.  See TAMB 10.2.3
  // for more about optional hooks.
  APR_OPTIONAL_HOOK(
      ssl,                        // prefix of optional hook
      npn_advertise_protos_hook,  // name of optional hook
      AdvertiseSpdy,              // hook function to be called
      NULL,                       // predecessors
      NULL,                       // successors
      APR_HOOK_MIDDLE);           // position
  // If we're advertising SPDY support via NPN, we ought to also advertise HTTP
  // support.  Ideally, the Apache core HTTP module would do this, but for now
  // it doesn't, so we'll do it for them.  We use APR_HOOK_LAST here, since
  // http/1.1 is our last choice.  Note that our AdvertiseHttp function won't
  // add "http/1.1" to the list if it's already there, so this is future-proof.
  APR_OPTIONAL_HOOK(ssl, npn_advertise_protos_hook,
                    AdvertiseHttp, NULL, NULL, APR_HOOK_LAST);

  // Register a hook with mod_ssl to be called when NPN has been completed and
  // the next protocol decided upon.  This hook will check if we're actually to
  // be using SPDY with the client, and enable this module if so.  This hook is
  // declared in mod_ssl.h, for appropriately-patched versions of mod_ssl.
  APR_OPTIONAL_HOOK(
      ssl,                        // prefix of optional hook
      npn_proto_negotiated_hook,  // name of optional hook
      OnNextProtocolNegotiated,   // hook function to be called
      NULL,                       // predecessors
      NULL,                       // successors
      APR_HOOK_MIDDLE);           // position

  // Register our input filter, and store the filter handle into a global
  // variable so we can use it later to instantiate our filter into a filter
  // chain.  The "filter type" argument below determines where in the filter
  // chain our filter will be placed.  We use AP_FTYPE_NETWORK so that we will
  // be at the very end of the input chain for slave connections, in place of
  // the usual core input filter.
  gSpdyToHttpFilterHandle = ap_register_input_filter(
      "SPDY_TO_HTTP",             // name
      SpdyToHttpFilter,           // filter function
      NULL,                       // init function (n/a in our case)
      AP_FTYPE_NETWORK);          // filter type

  // Now register our output filter, analogously to the input filter above.
  // Using AP_FTYPE_TRANSCODE allows us to convert from HTTP to SPDY at the end
  // of the protocol phase, so that we still have access to the HTTP headers as
  // a data structure (rather than raw bytes).  See TAMB 8.2 for a summary of
  // the different filter types.
  //
  // Even though we use AP_FTYPE_TRANSCODE, we expect to be the last filter in
  // the chain for slave connections, because we explicitly disable mod_ssl and
  // the core output filter for slave connections.  However, if another module
  // exists that uses a connection-level output filter, it may not work with
  // mod_spdy.  We should revisit this if that becomes a problem.
  gHttpToSpdyFilterHandle = ap_register_output_filter(
      "HTTP_TO_SPDY",             // name
      HttpToSpdyFilter,           // filter function
      NULL,                       // init function (n/a in our case)
      AP_FTYPE_TRANSCODE);        // filter type

  // This output filter is a hack to ensure that Httpd doesn't try to chunk our
  // output data (which would _not_ mix well with SPDY).  Using a filter type
  // of PROTOCOL-1 ensures that it runs just before the core HTTP_HEADER filter
  // (which is responsible for inserting the CHUNK filter).
  gAntiChunkingFilterHandle = ap_register_output_filter(
      "SPDY_ANTI_CHUNKING", AntiChunkingFilter, NULL,
      static_cast<ap_filter_type>(AP_FTYPE_PROTOCOL - 1));

  // Register our optional functions, so that other modules can retrieve and
  // use them.  See TAMB 10.1.2.
  APR_REGISTER_OPTIONAL_FN(spdy_get_version);
}

}  // namespace

extern "C" {

  // Export our module so Apache is able to load us.
  // See http://gcc.gnu.org/wiki/Visibility for more information.
#if defined(__linux)
#pragma GCC visibility push(default)
#endif

  // Declare our module object (note that "module" is a typedef for "struct
  // module_struct"; see http_config.h for the definition of module_struct).
  module AP_MODULE_DECLARE_DATA spdy_module = {
    // This next macro indicates that this is a (non-MPM) Apache 2.0 module
    // (the macro actually expands to multiple comma-separated arguments; see
    // http_config.h for the definition):
    STANDARD20_MODULE_STUFF,

    // These next four arguments are callbacks for manipulating configuration
    // structures (the ones we don't need are left null):
    NULL,  // create per-directory config structure
    NULL,  // merge per-directory config structures
    mod_spdy::CreateSpdyServerConfig,  // create per-server config structure
    mod_spdy::MergeSpdyServerConfigs,  // merge per-server config structures

    // This argument supplies a table describing the configuration directives
    // implemented by this module:
    mod_spdy::kSpdyConfigCommands,

    // Finally, this function will be called to register hooks for this module:
    RegisterHooks
  };

#if defined(__linux)
#pragma GCC visibility pop
#endif

}  // extern "C"
