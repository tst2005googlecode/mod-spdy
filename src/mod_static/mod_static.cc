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

#include <string>

#include "base/string_util.h"
#include "mod_static/http_response.pb.h"
#include "third_party/apache/apr/src/include/apr_strings.h"
#include "third_party/apache/httpd/src/include/httpd.h"
#include "third_party/apache/httpd/src/include/http_core.h"
#include "third_party/apache/httpd/src/include/http_config.h"
#include "third_party/apache/httpd/src/include/http_log.h"
#include "third_party/apache/httpd/src/include/http_protocol.h"
#include "third_party/apache/httpd/src/include/http_request.h"
#include "third_party/chromium/src/net/tools/flip_server/url_to_filename_encoder.h"

namespace {

// Define the static content root directory
const char* kStaticDir = "/static/";

// Default handler when the file is not found
int static_default_handler(const std::string& filename, request_rec* r) {
  ap_set_content_type(r, "text/html; charset=utf-8");
  ap_rputs("<html><head><title>Wow, cache server</title></head>", r);
  ap_rputs("<body><h1>Cache Server is running</h1>OK", r);
  ap_rputs("<hr>NOT FOUND:", r);
  ap_rputs(filename.c_str(), r);
  ap_rputs("</body></html>", r);
  return OK;
}

int send_response(const http_response::HttpResponse& response,
                  request_rec* r) {
  const http_response::HttpHeaders& headers = response.parsed_headers();
  const std::string& protocol_version = headers.protocol_version();

  // Apache2 defaults to set the status line as HTTP/1.1
  // If the original content was HTTP/1.0, we need to force the server
  // to use HTTP/1.0
  if (protocol_version == "HTTP/1.0") {
    apr_table_set(r->subprocess_env, "force-response-1.0", "1");
  }

  // Set the response status code
  r->status = headers.status_code();

  std::string content_type;
  int header_size = headers.headers_size();
  for (int idx = 0; idx < header_size; ++idx) {
    const http_response::HttpHeader header = headers.headers(idx);
    std::string lowercase_header = StringToLowerASCII(header.key());
    if (lowercase_header == "content-encoding") {
      // Skip original encoding, since the content is decoded.
      // The content can be gziped with mod_delate.
    } else if (lowercase_header == "content-length") {
      // Skip the original content-length. Always set the content-length
      // before sending the body.
    } else if (lowercase_header == "content-type") {
      // ap_set_content_type does not make a copy of the string, we need
      // to duplicate it.
      char* ptr = apr_pstrdup(r->pool,  header.value().c_str());
      ap_set_content_type(r, ptr);
    } else {
      // apr_table_add makes copies of both head key and value, so we do not
      // have to duplicate them.
      apr_table_add(r->headers_out,
                    header.key().c_str(),
                    header.value().c_str());
    }
  }

  // Recompute the content-length, because the content is decoded.
  ap_set_content_length(r, response.decoded_body().size());
  // Send the body
  ap_rwrite(response.decoded_body().c_str(),
            response.decoded_body().size(),
            r);

  return OK;
}

// Process decoded file -- ungziped, unchunked
int process_decoded_file(const std::string& filename, request_rec* r) {
  FILE* file = fopen(filename.c_str(), "rb");
  if (file == NULL) {
    ap_log_rerror(APLOG_MARK, APLOG_ERR, APR_SUCCESS, r,
                  "Can't open file %s", filename.c_str());
    return static_default_handler(filename, r);
  }

  int fd = fileno(file);
  http_response::HttpResponse response;
  if (!response.ParseFromFileDescriptor(fd)) {
    fclose(file);
    ap_log_rerror(APLOG_MARK, APLOG_ERR, APR_SUCCESS, r,
                  "Can't parse from fd for %s", filename.c_str());
    return static_default_handler(filename, r);
  }
  fclose(file);

  return send_response(response, r);
}



// Convert URI to encoded filename
std::string get_request_filename(request_rec* r) {
  std::string static_root = ap_document_root(r);
  static_root.append(kStaticDir);
  std::string hostname(r->hostname);
  std::string uri(r->unparsed_uri);
  std::string url = hostname + uri;
  std::string encoded_filename =
      net::UrlToFilenameEncoder::Encode(url, static_root);
  return encoded_filename;
}

int static_handler(request_rec* r) {
  // Check if the request is for our static content generator
  // Decline the request so that other handler may process
  if (!r->handler || strcmp(r->handler, "static")) {
    ap_log_rerror(APLOG_MARK, APLOG_WARNING, APR_SUCCESS, r,
                  "Not static request.");
    return DECLINED;
  }

  // Only handle GET request
  if (r->method_number != M_GET) {
    ap_log_rerror(APLOG_MARK, APLOG_WARNING, APR_SUCCESS, r,
                  "Not GET request: %d.", r->method_number);
    return HTTP_METHOD_NOT_ALLOWED;
  }
  std::string full_filename = get_request_filename(r);
  return process_decoded_file(full_filename, r);
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
