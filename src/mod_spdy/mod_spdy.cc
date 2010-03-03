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

#include "third_party/apache_httpd/include/httpd.h"
#include "third_party/apache_httpd/include/http_connection.h"
#include "third_party/apache_httpd/include/http_config.h"
#include "third_party/apache_httpd/include/http_log.h"
#include "third_party/apache_httpd/include/http_request.h"

#include "mod_spdy/apache/brigade_output_stream.h"
#include "mod_spdy/apache/log_message_handler.h"
#include "mod_spdy/apache/pool_util.h"
#include "mod_spdy/apache/response_header_populator.h"
#include "mod_spdy/apache/spdy_input_filter.h"
#include "mod_spdy/apache/spdy_output_filter.h"
#include "mod_spdy/common/connection_context.h"

extern "C" {
  // Forward declaration for the sake of ap_get_module_config and friends.
  extern module AP_MODULE_DECLARE_DATA spdy_module;
}

namespace {

// These two global variables store the filter handles for our input and output
// filters.  Normally, global variables would be very dangerous in a concurrent
// environment like Apache, but these ones are okay because they are assigned
// just once, at start-up (during which Apache is running single-threaded; see
// TAMB 2.2.1), and are read-only thereafter.
ap_filter_rec_t* g_spdy_output_filter;
ap_filter_rec_t* g_spdy_input_filter;

// See TAMB 8.4.2
apr_status_t spdy_input_filter(ap_filter_t* filter,
                               apr_bucket_brigade* bb,
                               ap_input_mode_t mode,
                               apr_read_type_e block,
                               apr_off_t readbytes) {
  mod_spdy::SpdyInputFilter* input_filter =
      static_cast<mod_spdy::SpdyInputFilter*>(filter->ctx);
  return input_filter->Read(filter, bb, mode, block, readbytes);
}

// See TAMB 8.4.1
apr_status_t spdy_output_filter(ap_filter_t* filter,
                                apr_bucket_brigade* input_brigade) {
  mod_spdy::SpdyOutputFilter* output_filter =
      static_cast<mod_spdy::SpdyOutputFilter*>(filter->ctx);
  return output_filter->Write(filter, input_brigade);
}

// Invoked once per request.  See http_request.h for details.
void spdy_insert_filter_hook(request_rec* request) {
  conn_rec* connection = request->connection;

  // Get the shared context object for this connection.  This object is created
  // and attached to the connection's configuration vector in the
  // spdy_pre_connection_hook function.
  mod_spdy::ConnectionContext* conn_context =
      static_cast<mod_spdy::ConnectionContext*>(ap_get_module_config(
          connection->conn_config,  // configuration vector to get from
          &spdy_module));  // module with which desired object is associated

  if (conn_context == NULL) {
    LOG(ERROR) << "No connection context present for mod_spdy.";
    return;
  }

  // Create a context object for this request's output filter.
  mod_spdy::SpdyOutputFilter* output_filter =
      new mod_spdy::SpdyOutputFilter(conn_context, request);
  PoolRegisterDelete(request->pool, output_filter);

  // Insert the output filter into this request's filter chain.
  ap_add_output_filter_handle(g_spdy_output_filter,  // filter handle
                              output_filter,  // context (any void* we want)
                              request,        // request object
                              connection);    // connection object
}

/**
 * Invoked once per connection. See http_connection.h for details.
 */
int spdy_pre_connection_hook(conn_rec* connection, void* csd) {
  // Create a shared context object for this connection; this object will be
  // used by both our input filter and our output filter.
  mod_spdy::ConnectionContext* context =
      new mod_spdy::ConnectionContext();
  PoolRegisterDelete(connection->pool, context);

  // Place the context object in the connection's configuration vector, so that
  // other hook functions with access to this connection can get hold of the
  // context object.  See TAMB 4.2 for details.
  ap_set_module_config(connection->conn_config,  // configuration vector
                       &spdy_module,  // module with which to associate
                       context);      // pointer to store (any void* we want)

  // Create a SpdyInputFilter object to be used by our input filter,
  // and register it with the connection's pool so that it will be
  // deallocated when this connection ends.
  mod_spdy::SpdyInputFilter *input_filter =
      new mod_spdy::SpdyInputFilter(connection);
  mod_spdy::PoolRegisterDelete(connection->pool, input_filter);

  // Add our input filter into the filter chain.  We use the
  // SpdyInputFilter as our context object, so that our input filter
  // will have access to it every time it runs.  The position of our
  // filter in the chain is (partially) determined by the filter type,
  // which is specified when our filter handle is created below in the
  // spdy_register_hook function.
  ap_add_input_filter_handle(g_spdy_input_filter,  // filter handle
                             input_filter,  // context (any void* we want)
                             NULL,  // request object (n/a for a conn filter)
                             connection);  // connection object

  // This hook should return OK (meaning we did something), DECLINED (meaning
  // we did nothing), or some error code (meaning something went wrong).  In
  // this case, we did something, and it went swimmingly, so we return OK.  See
  // http://httpd.apache.org/docs/2.0/developer/hooks.html#create-implement for
  // details, and see httpd.h for the definitions of OK and DECLINED.
  return OK;
}

// mod_ssl is AP_FTYPE_CONNECTION + 5.  We want to hook right after mod_ssl on
// input.
const ap_filter_type kSpdyInputFilterType =
    static_cast<ap_filter_type>(AP_FTYPE_CONNECTION + 4);

const ap_filter_type kSpdyOutputFilterType = AP_FTYPE_TRANSCODE;

void spdy_register_hook(apr_pool_t* p) {
  mod_spdy::InstallLogMessageHandler();

  // Let users know that they are installing an experimental module.
  LOG(WARNING) << "mod_spdy is currently an experimental Apache module. "
               << "It is not yet suitable for production environments "
               << "and may have stability issues.";

  // Register a hook to be called for each new connection.  This hook will
  // set up a shared context object and install our input filter for that
  // connection.  The "predecessors" and "successors" arguments can be used to
  // specify particular modules that must run before/after this one (see
  // http://httpd.apache.org/docs/2.0/developer/hooks.html#hooking-order),
  // but we don't care, so we omit those and use the "position" argument to
  // indicate that running somewhere in the middle would be just fine.
  ap_hook_pre_connection(
      spdy_pre_connection_hook,  // hook function to be called
      NULL,                      // predecessors
      NULL,                      // successors
      APR_HOOK_MIDDLE);          // position

  // Register a hook to be called when adding filters for each new request.
  // This hook will insert our output filter.
  ap_hook_insert_filter(
      spdy_insert_filter_hook,   // hook function to be called
      NULL,                      // predecessors
      NULL,                      // successors
      APR_HOOK_MIDDLE);          // position

  // Register our input filter, and store the filter handle into a global
  // variable so we can use it later to instantiate our filter into a filter
  // chain.  The "filter type" argument below determines where in the filter
  // chain our filter will be placed.
  g_spdy_input_filter = ap_register_input_filter(
      "SPDY-IN",              // name
      spdy_input_filter,      // filter function
      NULL,                   // init function (n/a in our case)
      kSpdyInputFilterType);  // filter type

  // Now register our output filter, analogously to the input filter above.
  // Using AP_FTYPE_TRANSCODE allows us to convert from HTTP to SPDY at the end
  // of the protocol phase, so that we still have access to the HTTP headers as
  // a data structure (rather than raw bytes).  Filters that run after
  // TRANSCODE are connection filters, which are supposed to be oblivious to
  // HTTP (and thus shouldn't break when run on SPDY data).  See TAMB 8.2 for a
  // summary of the different filter types.
  g_spdy_output_filter = ap_register_output_filter(
      "SPDY-OUT",              // name
      spdy_output_filter,      // filter function
      NULL,                    // init function (n/a in our case)
      kSpdyOutputFilterType);  // filter type
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
    spdy_register_hook
  };

#if defined(__linux)
#pragma GCC visibility pop
#endif

}
