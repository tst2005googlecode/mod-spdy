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

#ifndef MOD_SPDY_APACHE_CONFIG_UTIL_H_
#define MOD_SPDY_APACHE_CONFIG_UTIL_H_

#include "httpd.h"
#include "http_config.h"

namespace mod_spdy {

class ConnectionContext;
class SpdyServerConfig;
class SpdyStream;

// Get the server configuration associated with the given object.  The
// configuration object is returned const, since by the time these functions
// are being used, the configuration should be treated as read-only.
const SpdyServerConfig* GetServerConfig(server_rec* server);
const SpdyServerConfig* GetServerConfig(conn_rec* connection);
const SpdyServerConfig* GetServerConfig(request_rec* request);

// Get the server configuration associated with the given configuration command
// parameters.  Since this is for setting the configuration (rather than just
// reading it), the configuration object is returned non-const.
SpdyServerConfig* GetServerConfig(cmd_parms* command);

// Allocate a new ConnectionContext object for a master connection in the given
// connection's pool, attach it to the connection's config vector, and return
// it.
ConnectionContext* CreateMasterConnectionContext(conn_rec* connection,
                                                 bool using_ssl);

// Allocate a new ConnectionContext object for a slave connection in the given
// connection's pool, attach it to the connection's config vector, and return
// it.
ConnectionContext* CreateSlaveConnectionContext(conn_rec* connection,
                                                bool using_ssl,
                                                SpdyStream* stream);

// Get the connection object that was attached to this connection by
// CreateConnectionContext; return NULL if CreateConnectionContext has not been
// called for this connection, which will be the case if 1) mod_spdy is
// disabled on this server, 2) this is a non-SSL connection, or 3) this is not
// a slave connection, and the pre-connection hook hasn't fired yet.
ConnectionContext* GetConnectionContext(conn_rec* connection);

}  // namespace mod_spdy

#endif  // MOD_SPDY_APACHE_CONFIG_UTIL_H_
