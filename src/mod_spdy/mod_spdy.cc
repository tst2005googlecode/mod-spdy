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

extern "C" {
#include "httpd.h"
#include "http_connection.h"
#include "http_config.h"
#include "http_log.h"
#include "http_request.h"
}

#include "net/flip/flip_frame_builder.h"
#include "net/flip/flip_framer.h"

namespace {

// These two global variables store the filter handles for our input and output
// filters.  Normally, global variables would be very dangerous in a concurrent
// environment like Apache, but these ones are okay because they are assigned
// just once, at start-up (during which Apache is running single-threaded; see
// TAMB 2.2.1), and are read-only thereafter.
ap_filter_rec_t* g_spdy_output_filter;
ap_filter_rec_t* g_spdy_input_filter;

// Helper that logs information associated with a filter.  Print the given
// message, prepended with the connection ID and the filter name.
void TRACE_FILTER(ap_filter_t* f, const char* msg) {
  ap_log_cerror(APLOG_MARK,    // file/line
                APLOG_NOTICE,  // level
                APR_SUCCESS,   // status
                f->c,          // connection
                "%ld %s: %s", f->c->id, f->frec->name, msg);
}

// See TAMB 8.4.2
apr_status_t spdy_input_filter(ap_filter_t* f,
                               apr_bucket_brigade* bb,
                               ap_input_mode_t mode,
                               apr_read_type_e block,
                               apr_off_t readbytes) {
  // TODO: Implement this filter!
  TRACE_FILTER(f, "Input");
  return ap_get_brigade(f->next, bb, mode, block, readbytes);
}

// See TAMB 8.4.1
apr_status_t spdy_output_filter(ap_filter_t* f,
                                apr_bucket_brigade* bb) {
  // TODO: Implement this filter!
  TRACE_FILTER(f, "Output");
  return ap_pass_brigade(f->next, bb);
}

apr_status_t FlipFramerDeleter(void* framer) {
  delete static_cast<flip::FlipFramer*>(framer);
  return APR_SUCCESS;
}

apr_status_t FlipFrameBuilderDeleter(void* builder) {
  delete static_cast<flip::FlipFrameBuilder*>(builder);
  return APR_SUCCESS;
}

/**
 * Invoked once per connection. See http_connection.h for details.
 */
int spdy_pre_connection_hook(conn_rec* c, void* csd) {
  ap_log_cerror(APLOG_MARK,    // file/line
                APLOG_NOTICE,  // level
                APR_SUCCESS,   // status
                c,             // connection
                "%ld Registering SPDY filters", c->id);  // format and args

  // Create a FlipFramer object to be used by our input filter, and register it
  // with the connection's pool so that it will be deallocated when this
  // connection ends.  (Note that the "child cleanup" argument below doesn't
  // apply to us, so we use apr_pool_cleanup_null, which is a no-op cleanup
  // function.)
  flip::FlipFramer *framer = new flip::FlipFramer();
  apr_pool_cleanup_register(c->pool,                 // pool
                            framer,                  // object
                            FlipFramerDeleter,       // cleanup function
                            apr_pool_cleanup_null);  // child cleanup

  // Add our input filter into the filter chain.  We use the FlipFramer as our
  // context object, so that our input filter will have access to it every time
  // it runs.  The position of our filter in the chain is (partially)
  // determined by the filter type, which is specified when our filter handle
  // is created below in the spdy_register_hook function.
  ap_add_input_filter_handle(g_spdy_input_filter,  // filter handle
                             framer,  // context object (any void* we want)
                             NULL,    // request object (n/a for a conn filter)
                             c);      // connection object

  // Now set up the output filter, analogously to the input filter.  The same
  // comments apply here as for the input filter above, so we will elide them.
  flip::FlipFrameBuilder *builder = new flip::FlipFrameBuilder();
  apr_pool_cleanup_register(c->pool,
                            builder,
                            FlipFrameBuilderDeleter,
                            apr_pool_cleanup_null);
  ap_add_output_filter_handle(g_spdy_output_filter, builder, NULL, c);

  // This hook should return OK (meaning we did something), DECLINED (meaning
  // we did nothing), or some error code (meaning something went wrong).  In
  // this case, we did something, and it went swimmingly, so we return OK.  See
  // http://httpd.apache.org/docs/2.0/developer/hooks.html#create-implement for
  // details, and see httpd.h for the definitions of OK and DECLINED.
  return OK;
}

// mod_ssl is AP_FTYPE_CONNECTION + 5.  We want to hook right before mod_ssl on
// output (right after mod_ssl on input).
const ap_filter_type kSpdyFilterType =
    static_cast<ap_filter_type>(AP_FTYPE_CONNECTION + 4);

void spdy_register_hook(apr_pool_t* p) {
  // Register a hook to be called for each new connection.  Our hook will
  // install our input and output filters into the filter chain for that
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

  // Register our input filter, and store the filter handle into a global
  // variable so we can use it later to instantiate our filter into a filter
  // chain.  The "filter type" argument below determines where in the filter
  // chain our filter will be placed.
  g_spdy_input_filter = ap_register_input_filter(
      "SPDY-IN",          // name
      spdy_input_filter,  // filter function
      NULL,               // init function (n/a in our case)
      kSpdyFilterType);   // filter type

  // Now register our output filter, analogously to the input filter above.
  g_spdy_output_filter = ap_register_output_filter(
      "SPDY-OUT", spdy_output_filter, NULL, kSpdyFilterType);
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
