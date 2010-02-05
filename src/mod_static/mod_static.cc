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

extern "C" {
#include "httpd.h"
#include "http_config.h"
#include "http_request.h"
#include "http_protocol.h"
#include "http_log.h"
}

namespace {

int static_handler(request_rec* r) {
  // check if request for static
  if (!r->handler || strcmp(r->handler,"static")) {
    ap_log_rerror(__FILE__,__LINE__,APLOG_DEBUG, 0, r,
                  "Not static request.");
    return DECLINED;
  }

  // Only handle GET request
  if (r->method_number != M_GET) {
    ap_log_rerror(__FILE__,__LINE__,APLOG_DEBUG, 0, r,
                  "Not GET request: %d.", r->method_number);
    return HTTP_METHOD_NOT_ALLOWED;
  }

  // TODO remove DEBUG info
  ap_log_rerror(__FILE__,__LINE__,APLOG_INFO, 0, r,
                  "URI(unparsed): %s", r->unparsed_uri);
  ap_log_rerror(__FILE__,__LINE__,APLOG_INFO, 0, r,
                  "URI: %s", r->uri);
  ap_log_rerror(__FILE__,__LINE__,APLOG_INFO, 0, r,
                  "hostname: %s", r->hostname);
  ap_log_rerror(__FILE__,__LINE__,APLOG_INFO, 0, r,
                  "filename: %s", r->filename);




  // TODO respond with the file.
  ap_set_content_type(r, "text/html; charset=utf-8");
  apr_table_setn(r->headers_out, "Server", "static");
  apr_table_setn(r->headers_out, "X-info", "static");
  apr_table_setn(r->headers_out, "Set-Cookie", "mod_static=1.000");
  ap_rputs("<html><head><title>Wow, cache server</title></head>", r);
  ap_rputs("<body><h1>Cache Server is running</h1>OK", r);
  ap_rputs("<hr>",r);
  ap_rputs("</body></html>", r);
  return OK;
}

void static_hook(apr_pool_t* p) {
  // Register the content generator, or handler.
  ap_hook_handler(static_handler, NULL, NULL, APR_HOOK_MIDDLE);
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
  module AP_MODULE_DECLARE_DATA static_module = {
    // This next macro indicates that this is a (non-MPM) Apache 2.0 module
    // (the macro actually expands to multiple comma-separated arguments; see
    // http_config.h for the definition):
    STANDARD20_MODULE_STUFF,

    // These next four arguments are callbacks, but we currently don't need
    // them, so they are left null:
    NULL,  // create per-directory config structure
    NULL,  // merge per-directory config structures
    NULL,  // create per-server config structure
    NULL,  // merge per-server config structures

    // This argument supplies a table describing the configuration directives
    // implemented by this module (however, we don't currently have any):
    NULL,

    // Finally, this function will be called to register hooks for this module:
    static_hook
  };

#if defined(__linux)
#pragma GCC visibility pop
#endif

}
