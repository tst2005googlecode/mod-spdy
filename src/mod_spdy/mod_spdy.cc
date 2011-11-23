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

#include "httpd.h"
#include "http_connection.h"
#include "http_config.h"
#include "http_request.h"
#include "apr_optional.h"
#include "apr_optional_hooks.h"
#include "apr_tables.h"

#include "base/string_piece.h"

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

  // Declaring modified mod_ssl's optional hooks here (so that we don't need to
  // #include "mod_ssl.h").
  APR_DECLARE_OPTIONAL_FN(int, ssl_is_https, (conn_rec *));
  APR_DECLARE_EXTERNAL_HOOK(
      ssl, AP, int, npn_advertise_protos_hook,
      (conn_rec* connection, apr_array_header_t* protos));
  APR_DECLARE_EXTERNAL_HOOK(
      ssl, AP, int, npn_proto_negotiated_hook,
      (conn_rec* connection, char* proto_name, apr_size_t proto_name_len));
}

namespace {

// For now, we only support SPDY version 2.
const char* kSpdyProtocolName = "spdy/2";

// These global variables store the filter handles for our filters.  Normally,
// global variables would be very dangerous in a concurrent environment like
// Apache, but these ones are okay because they are assigned just once, at
// start-up (during which Apache is running single-threaded; see TAMB 2.2.1),
// and are read-only thereafter.
ap_filter_rec_t* g_spdy_output_filter;
ap_filter_rec_t* g_spdy_input_filter;
ap_filter_rec_t* g_anti_chunking_filter;

// Get our context object for the given connection.
mod_spdy::ConnectionContext* GetConnectionContext(conn_rec* connection) {
  // Get the shared context object for this connection.  This object is created
  // and attached to the connection's configuration vector in
  // spdy_pre_connection().
  return static_cast<mod_spdy::ConnectionContext*>(ap_get_module_config(
      connection->conn_config,  // configuration vector to get from
      &spdy_module));  // module with which desired object is associated
}

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
  // Make sure nothing unexpected has happened to the transfer encoding between
  // here and our anti-chunking filter.
  request_rec* request = filter->r;
  if (filter->r->chunked != 0) {
    LOG(DFATAL) << "spdy_output_filter: request->chunked == "
                << request->chunked << " in request "
                << request->the_request;
  }
  const char* transfer_encoding =
      apr_table_get(filter->r->headers_out, "Transfer-Encoding");
  if (transfer_encoding != NULL && strcmp(transfer_encoding, "chunked")) {
    LOG(DFATAL) << "anti_chunking_filter: transfer_encoding == \""
                << transfer_encoding << "\"" << " in request "
                << request->the_request;
  }
  // Remove the transfer-encoding header so that it does not appear in our SPDY
  // headers.
  apr_table_unset(request->headers_out, "Transfer-Encoding");
  mod_spdy::SpdyOutputFilter* output_filter =
      static_cast<mod_spdy::SpdyOutputFilter*>(filter->ctx);
  return output_filter->Write(filter, input_brigade);
}

apr_status_t anti_chunking_filter(ap_filter_t* filter,
                                  apr_bucket_brigade* input_brigade) {
  request_rec* request = filter->r;
  // Make sure no one is already trying to chunk the data in this request.
  if (request->chunked != 0) {
    LOG(DFATAL) << "anti_chunking_filter: request->chunked == "
                << request->chunked << " in request "
                << request->the_request;
  }
  const char* transfer_encoding =
      apr_table_get(request->headers_out, "Transfer-Encoding");
  if (transfer_encoding != NULL) {
    LOG(DFATAL) << "anti_chunking_filter: transfer_encoding == \""
                << transfer_encoding << "\"" << " in request "
                << request->the_request;
  }
  // Setting the Transfer-Encoding header to "chunked" here will trick the core
  // HTTP_HEADER filter into not inserting the CHUNK filter.
  apr_table_setn(request->headers_out, "Transfer-Encoding", "chunked");
  // This filter only needs to run once, so now that it has run, remove it.
  ap_remove_output_filter(filter);
  return ap_pass_brigade(filter->next, input_brigade);
}

// Called by mod_ssl when it needs to decide what protocols to advertise to the
// client during Next Protocol Negotiation (NPN).
int spdy_npn_advertise_protos(conn_rec* connection,
                              apr_array_header_t* protos) {
  // TODO(mdsteele): Check server config for this connection to see if we have
  //   enabled SPDY on the relevant vhost (or whatever).
  APR_ARRAY_PUSH(protos, const char*) = kSpdyProtocolName;
  return OK;
}

// Called by mod_ssl after Next Protocol Negotiation (NPN) has completed,
// informing us which protocol was chosen by the client.
int spdy_npn_proto_negotiated(conn_rec* connection, char* proto_name,
                              apr_size_t proto_name_len) {
  mod_spdy::ConnectionContext* conn_context = GetConnectionContext(connection);
  if (conn_context == NULL) {
    LOG(ERROR) << "No connection context present for mod_spdy.";
    return DECLINED;
  }

  if (base::StringPiece(proto_name, proto_name_len) == kSpdyProtocolName) {
    conn_context->set_npn_state(mod_spdy::ConnectionContext::USING_SPDY);
  } else {
    conn_context->set_npn_state(mod_spdy::ConnectionContext::NOT_USING_SPDY);
  }
  return OK;
}

// Invoked once per request.  See http_request.h for details.
void spdy_insert_filter(request_rec* request) {
  conn_rec* connection = request->connection;
  mod_spdy::ConnectionContext* conn_context = GetConnectionContext(connection);

  // If a connection context wasn't created in our pre_connection hook, that
  // indicates that we won't be using SPDY for this connection (e.g. because
  // we're not using SSL for this connection).  In that case, don't insert our
  // filters.
  if (conn_context == NULL) {
    return;
  }

  // If NPN didn't choose SPDY for this connection, don't insert our filters.
  if (conn_context->npn_state() != mod_spdy::ConnectionContext::USING_SPDY) {
    return;
  }

  // Create a context object for this request's output filter.
  mod_spdy::SpdyOutputFilter* output_filter =
      new mod_spdy::SpdyOutputFilter(conn_context, request);
  PoolRegisterDelete(request->pool, output_filter);

  // Insert our output filters into this request's filter chain.
  ap_add_output_filter_handle(g_spdy_output_filter,  // filter handle
                              output_filter,  // context (any void* we want)
                              request,        // request object
                              connection);    // connection object
  ap_add_output_filter_handle(g_anti_chunking_filter,
                              NULL, request, connection);
}

// Invoked once per connection. See http_connection.h for details.
int spdy_pre_connection(conn_rec* connection, void* csd) {
  // We do not want to attach to non-inbound connections
  // (e.g. connections created by mod_proxy). Non-inbound connections
  // do not get a scoreboard hook, so we abort if the connection
  // doesn't have the scoreboard hook. See
  // http://mail-archives.apache.org/mod_mbox/httpd-dev/201008.mbox/%3C99EA83DCDE961346AFA9B5EC33FEC08B047FDC26@VF-MBX11.internal.vodafone.com%3E
  // for more details.
  if (connection->sbh == NULL) {
    return DECLINED;
  }

  // Check if this connection is over SSL; if not, we definitely won't be using
  // SPDY.  Note that ssl_is_https() must be called _after_ mod_ssl's
  // pre_connection hook has run, so we specify in spdy_register_hooks() that
  // our pre_connection hook function must run after mod_ssl's.
  int (*using_ssl)(conn_rec*) = APR_RETRIEVE_OPTIONAL_FN(ssl_is_https);
  if (using_ssl == NULL ||  // mod_ssl is not even loaded
      using_ssl(connection) == 0) {
    // This is not an SSL connection, so we can't talk SPDY on it.
    return DECLINED;
  }

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
      new mod_spdy::SpdyInputFilter(connection, context);
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

void spdy_register_hooks(apr_pool_t* p) {
  mod_spdy::InstallLogMessageHandler(p);

  // Let users know that they are installing an experimental module.
  LOG(WARNING) << "mod_spdy is currently an experimental Apache module. "
               << "It is not yet suitable for production environments "
               << "and may have stability issues.";

  // Register a hook to be called for each new connection.  This hook will set
  // up a shared context object and install our input filter for that
  // connection.  Our pre-connection hook must run after mod_ssl's, so that in
  // ours we can ask mod_ssl if the connection is using SSL.  See TAMB 10.2.2
  // or http://httpd.apache.org/docs/trunk/developer/hooks.html#hooking-order
  // for more about controlling hook order.
  static const char* const pre_connection_preds[] = {"mod_ssl.c", NULL};
  ap_hook_pre_connection(
      spdy_pre_connection,       // hook function to be called
      pre_connection_preds,      // predecessors
      NULL,                      // successors
      APR_HOOK_MIDDLE);          // position

  // Register a hook to be called when adding filters for each new request.
  // This hook will insert our output filter.
  ap_hook_insert_filter(
      spdy_insert_filter,        // hook function to be called
      NULL,                      // predecessors
      NULL,                      // successors
      APR_HOOK_MIDDLE);          // position

  // Register a hook with mod_ssl to be called when deciding what protocols to
  // advertise during Next Protocol Negotiatiation (NPN); we'll use this
  // opportunity to advertise that we support SPDY.  This hook is declared in
  // mod_ssl.h.  See TAMB 10.2.3 for more about optional hooks.
  APR_OPTIONAL_HOOK(
      ssl,                        // prefix of optional hook
      npn_advertise_protos_hook,  // name of optional hook
      spdy_npn_advertise_protos,  // hook function to be called
      NULL,                       // predecessors
      NULL,                       // successors
      APR_HOOK_MIDDLE);           // position

  // Register a hook with mod_ssl to be called when NPN has been completed and
  // the next protocol decided upon.  This hook will check if we're actually to
  // be using SPDY with the client, and enable this module if so.  This hook is
  // declared in mod_ssl.h.
  APR_OPTIONAL_HOOK(
      ssl,                        // prefix of optional hook
      npn_proto_negotiated_hook,  // name of optional hook
      spdy_npn_proto_negotiated,  // hook function to be called
      NULL,                       // predecessors
      NULL,                       // successors
      APR_HOOK_MIDDLE);           // position

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

  // This output filter is a hack to ensure that Httpd doesn't try to chunk our
  // output data (which would _not_ mix well with SPDY).  Using a filter type
  // of PROTOCOL-1 ensures that it runs just before the core HTTP_HEADER filter
  // (which is responsible for inserting the CHUNK filter).
  g_anti_chunking_filter = ap_register_output_filter(
      "SPDY-ANTICHUNK", anti_chunking_filter, NULL,
      static_cast<ap_filter_type>(AP_FTYPE_PROTOCOL - 1));
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
    spdy_register_hooks
  };

#if defined(__linux)
#pragma GCC visibility pop
#endif

}
