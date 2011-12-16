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

#include "mod_spdy/apache/config_util.h"

#include "httpd.h"
#include "http_config.h"

#include "base/logging.h"

#include "mod_spdy/apache/pool_util.h"
#include "mod_spdy/common/connection_context.h"
#include "mod_spdy/common/spdy_server_config.h"

extern "C" {
  extern module AP_MODULE_DECLARE_DATA spdy_module;
}

namespace mod_spdy {

namespace {

SpdyServerConfig* GetServerConfigInternal(server_rec* server) {
  void* ptr = ap_get_module_config(server->module_config, &spdy_module);
  CHECK(ptr) << "mod_spdy server config pointer is NULL";
  return static_cast<SpdyServerConfig*>(ptr);
}

ConnectionContext* SetConnContextInternal(conn_rec* connection,
                                          ConnectionContext* context) {
  PoolRegisterDelete(connection->pool, context);

  // Place the context object in the connection's configuration vector, so that
  // other hook functions with access to this connection can get hold of the
  // context object.  See TAMB 4.2 for details.
  ap_set_module_config(connection->conn_config,  // configuration vector
                       &spdy_module,  // module with which to associate
                       context);      // pointer to store (any void* we want)

  return context;
}

}  // namespace

const SpdyServerConfig* GetServerConfig(server_rec* server) {
  return GetServerConfigInternal(server);
}

const SpdyServerConfig* GetServerConfig(conn_rec* connection) {
  return GetServerConfigInternal(connection->base_server);
}

const SpdyServerConfig* GetServerConfig(request_rec* request) {
  return GetServerConfigInternal(request->server);
}

SpdyServerConfig* GetServerConfig(cmd_parms* command) {
  return GetServerConfigInternal(command->server);
}

ConnectionContext* CreateMasterConnectionContext(conn_rec* connection) {
  return SetConnContextInternal(connection, new ConnectionContext());
}

ConnectionContext* CreateSlaveConnectionContext(conn_rec* connection,
                                                SpdyStream* stream) {
  return SetConnContextInternal(connection, new ConnectionContext(stream));
}

ConnectionContext* GetConnectionContext(conn_rec* connection) {
  // Get the shared context object for this connection.  This object is created
  // and attached to the connection's configuration vector in
  // spdy_pre_connection().
  return static_cast<ConnectionContext*>(ap_get_module_config(
      connection->conn_config,  // configuration vector to get from
      &spdy_module));  // module with which desired object is associated
}

}  // namespace mod_spdy
