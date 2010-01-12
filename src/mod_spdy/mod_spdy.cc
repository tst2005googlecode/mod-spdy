extern "C" {
#include "apr_buckets.h"
#include "apr_strings.h"
#include "ap_config.h"
#include "util_filter.h"
#include "httpd.h"
#include "http_config.h"
#include "http_request.h"
#include "http_core.h"
#include "http_protocol.h"
#include "http_log.h"
#include "http_main.h"
#include "util_script.h"
#include "http_core.h"                                                             
}

namespace {

int spdy_init(ap_filter_t *f) {
//	if (f->c) ap_log_cerror(__FILE__, __LINE__, APLOG_NOTICE, APR_SUCCESS, f->c, "Init");
	return APR_SUCCESS;
}

apr_status_t spdy_filter(ap_filter_t *f, apr_bucket_brigade *bb) {
//	ap_log_cerror(__FILE__, __LINE__, APLOG_NOTICE, APR_SUCCESS, f->c, "Here I am %d", 1);
	return ap_pass_brigade(f->next, bb);
}

void spdy_register_hook(apr_pool_t *p) {
    ap_register_output_filter("SPDY", 
		                      spdy_filter, 
							  spdy_init, 
							  static_cast<ap_filter_type>(AP_FTYPE_PROTOCOL + 4));
}

}  // namespace

extern "C" {

module AP_MODULE_DECLARE_DATA spdy_module = {
    STANDARD20_MODULE_STUFF,
    NULL,			/* create per-directory config structure */
    NULL,        		/* merge per-directory config structures */
    NULL,			/* create per-server config structure */
    NULL,			/* merge per-server config structures */
    NULL,			/* command apr_table_t */
    spdy_register_hook		/* register hooks */
};

}