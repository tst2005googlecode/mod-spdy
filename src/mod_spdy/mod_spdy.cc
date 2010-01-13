extern "C" {
#include "httpd.h"
#include "http_connection.h"
#include "http_config.h"
#include "http_log.h"
#include "http_request.h"
}

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

/**
 * Runs once per request, once for each filter.
 */
int spdy_init_filter(ap_filter_t *f) {
    TRACE_FILTER(f, "Initializing");
    return APR_SUCCESS;
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

/**
 * Invoked once per connection. See http_connection.h for details.
 */
int spdy_pre_connection_hook(conn_rec *c, void *csd) {
    ap_log_cerror(APLOG_MARK,
                  APLOG_NOTICE,
                  APR_SUCCESS,
                  c,
                  "%ld Registering SPDY filters", c->id);

    // TODO: define and initialize shared context.
    void* context = NULL;
    ap_add_input_filter_handle(g_spdy_input_filter, context, NULL, c);
    ap_add_output_filter_handle(g_spdy_output_filter, context, NULL, c);
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
        spdy_init_filter,
        kSpdyFilterType);

    g_spdy_output_filter = ap_register_output_filter(
        "SPDY-OUT",
        spdy_output_filter,
        spdy_init_filter,
        kSpdyFilterType);
}

}  // namespace

extern "C" {

module AP_MODULE_DECLARE_DATA spdy_module = {
    STANDARD20_MODULE_STUFF,
    NULL,               /* create per-directory config structure */
    NULL,               /* merge per-directory config structures */
    NULL,               /* create per-server config structure */
    NULL,               /* merge per-server config structures */
    NULL,               /* command apr_table_t */
    spdy_register_hook  /* register hooks */
};

}
