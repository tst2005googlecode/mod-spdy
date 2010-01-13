extern "C" {
#include "httpd.h"
#include "http_config.h"
#include "http_log.h"
#include "http_request.h"
#include "util_filter.h"
}

namespace {

ap_filter_rec_t* g_spdy_output_filter;

int spdy_init(ap_filter_t *f) {
    ap_log_cerror(__FILE__, __LINE__, APLOG_NOTICE, APR_SUCCESS, f->c, "Init");
    return APR_SUCCESS;
}

apr_status_t spdy_filter(ap_filter_t *f, apr_bucket_brigade *bb) {
    ap_log_cerror(__FILE__, __LINE__, APLOG_NOTICE, APR_SUCCESS, f->c, "Filter");
    return ap_pass_brigade(f->next, bb);
}

void spdy_insert_filter_hook(request_rec *r) {
    ap_log_rerror(__FILE__, __LINE__, APLOG_NOTICE, APR_SUCCESS, r, "Registering SPDY");
    ap_add_output_filter_handle(g_spdy_output_filter, NULL, r, r->connection) ;
}

// mod_ssl is AP_FTYPE_CONNECTION + 5. We want to hook right before mod_ssl.
const ap_filter_type kSpdyFilterType =
    static_cast<ap_filter_type>(AP_FTYPE_CONNECTION + 4);

void spdy_register_hook(apr_pool_t *p) {
    ap_hook_insert_filter(spdy_insert_filter_hook,
        NULL,
        NULL,
        APR_HOOK_MIDDLE) ;

    g_spdy_output_filter = ap_register_output_filter(
        "SPDY",
        spdy_filter,
        spdy_init,
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