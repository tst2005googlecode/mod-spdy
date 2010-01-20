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
#include "http_connection.h"
#include "http_config.h"
#include "http_log.h"
#include "http_request.h"
}

#include "net/flip/flip_frame_builder.h"
#include "net/flip/flip_framer.h"

namespace {

ap_filter_rec_t* g_spdy_output_filter;
ap_filter_rec_t* g_spdy_input_filter;

// Helper that logs information associated with a filter.
void TRACE_FILTER(ap_filter_t *f, const char *msg) {
  ap_log_cerror(APLOG_MARK,
                APLOG_NOTICE,
                APR_SUCCESS,
                f->c,
                "%ld %s: %s", f->c->id, f->frec->name, msg);
}

apr_status_t spdy_input_filter(ap_filter_t *f,
                               apr_bucket_brigade *bb,
                               ap_input_mode_t mode,
                               apr_read_type_e block,
                               apr_off_t readbytes) {
  TRACE_FILTER(f, "Input");
  return ap_get_brigade(f->next, bb, mode, block, readbytes);
}

apr_status_t spdy_output_filter(ap_filter_t *f,
                                apr_bucket_brigade *bb) {
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
int spdy_pre_connection_hook(conn_rec *c, void *csd) {
  ap_log_cerror(APLOG_MARK,
                APLOG_NOTICE,
                APR_SUCCESS,
                c,
                "%ld Registering SPDY filters", c->id);

  // Set up the input filter.
  flip::FlipFramer *framer = new flip::FlipFramer();
  apr_pool_cleanup_register(c->pool,
                            framer,
                            FlipFramerDeleter,
                            apr_pool_cleanup_null);
  ap_add_input_filter_handle(g_spdy_input_filter, framer, NULL, c);

  // Set up the output filter.
  flip::FlipFrameBuilder *builder = new flip::FlipFrameBuilder();
  apr_pool_cleanup_register(c->pool,
                            builder,
                            FlipFrameBuilderDeleter,
                            apr_pool_cleanup_null);
  ap_add_output_filter_handle(g_spdy_output_filter, builder, NULL, c);

  return APR_SUCCESS;
}

// mod_ssl is AP_FTYPE_CONNECTION + 5. We want to hook right before mod_ssl.
const ap_filter_type kSpdyFilterType =
    static_cast<ap_filter_type>(AP_FTYPE_CONNECTION + 4);

void spdy_register_hook(apr_pool_t *p) {
  ap_hook_pre_connection(
      spdy_pre_connection_hook,
      NULL,
      NULL,
      APR_HOOK_MIDDLE);

  g_spdy_input_filter = ap_register_input_filter(
      "SPDY-IN",
      spdy_input_filter,
      NULL,
      kSpdyFilterType);

  g_spdy_output_filter = ap_register_output_filter(
      "SPDY-OUT",
      spdy_output_filter,
      NULL,
      kSpdyFilterType);
}

}  // namespace

extern "C" {

  // Export our module so Apache is able to load us.
  // See http://gcc.gnu.org/wiki/Visibility for more information.
#if defined(__linux)
#pragma GCC visibility push(default)
#endif

  module AP_MODULE_DECLARE_DATA spdy_module = {
    STANDARD20_MODULE_STUFF,
    NULL,               /* create per-directory config structure */
    NULL,               /* merge per-directory config structures */
    NULL,               /* create per-server config structure */
    NULL,               /* merge per-server config structures */
    NULL,               /* command apr_table_t */
    spdy_register_hook  /* register hooks */
  };

#if defined(__linux)
#pragma GCC visibility pop
#endif

}
