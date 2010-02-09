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

#include "third_party/apache_httpd/include/httpd.h"
#include "third_party/apache_httpd/include/http_config.h"
#include "third_party/apache_httpd/include/http_log.h"
#include "third_party/apache_httpd/include/http_protocol.h"
#include "third_party/apache_httpd/include/http_request.h"

namespace {

int static_default_handler(const std::string& filename, request_rec* r) {
  ap_set_content_type(r, "text/html; charset=utf-8");
  apr_table_setn(r->headers_out, "Server", "static");
  apr_table_setn(r->headers_out, "X-info", "static");
  apr_table_setn(r->headers_out, "Set-Cookie", "mod_static=1.000");
  ap_rputs("<html><head><title>Wow, cache server</title></head>", r);
  ap_rputs("<body><h1>Cache Server is running</h1>OK Daniel Song", r);
  ap_rputs("<hr>", r);
  ap_rputs(filename.c_str(), r);
  ap_rputs("</body></html>", r);

  return OK;
}

const char* get_next_line(const char* buffer, int* size, std::string* line) {
  line->clear();
  if (buffer == NULL) {
    return NULL;
  }

  const char* pointer = buffer;
  for (; *size > 0; --*size, ++pointer) {
    if (*pointer == '\r' && *(pointer + 1) == '\n') {
      *size -= 2;
      return pointer + 2;
    }
    line->push_back(*pointer);
  }
  return NULL;
}
int process_file(const std::string& filename, request_rec* r) {

  apr_file_t* fd = NULL;
  apr_status_t rv =
      apr_file_open(&fd, filename.c_str(),
                    APR_READ | APR_SHARELOCK | APR_SENDFILE_ENABLED,
                    APR_OS_DEFAULT, r->pool);
  if (rv != APR_SUCCESS) {
    ap_log_rerror(APLOG_MARK, APLOG_ERR, APR_SUCCESS, r,
                  "Can't open %s", filename.c_str());
    //return HTTP_NOT_FOUND;
    return static_default_handler(filename, r);
  }
  ap_log_rerror(APLOG_MARK, APLOG_INFO, APR_SUCCESS, r,
                "Opened %s", filename.c_str());

  apr_finfo_t finfo;
  rv = apr_file_info_get(&finfo, APR_FINFO_SIZE, fd);
  if (rv != APR_SUCCESS) {
    ap_log_rerror(APLOG_MARK, APLOG_ERR, APR_SUCCESS, r,
                  "Can't get file size of %s", filename.c_str());
    //return HTTP_NOT_FOUND;
    return static_default_handler(filename, r);
  }
  ap_log_rerror(APLOG_MARK, APLOG_INFO, APR_SUCCESS, r,
                "Get file size=%d", finfo.size);

//  apr_size_t sz;
//  ap_send_fd(fd, r, 0, finfo.size, &sz);
//  apr_file_close(fd);

  char* buffer = new char[finfo.size];
  apr_size_t file_size = finfo.size;
  rv = apr_file_read(fd, buffer, &file_size);

  // loop through the headers
  bool in_header = true;
  const char* header = buffer;
  int size = file_size;
  std::string line;
  bool first_line = true;
  while (in_header) {
    header = get_next_line(header, &size, &line);
    if ( first_line ) {
      if (line.compare(0, 8 ,"HTTP/1.0")) {
        apr_table_set(r->subprocess_env, "force-response-1.0", "1");
      }
      first_line = false;
      continue;
    }

    if (line.empty()) {
      // empty line; end of header
      break;
    }

    // parse headers
    std::size_t pos = line.find(':');
    if (pos == std::string::npos) {
      ap_log_rerror(APLOG_MARK, APLOG_ERR, APR_SUCCESS, r,
                   "Unkown header: %s", line.c_str());
    }
    std::string header = line.substr(0, pos);
    std::string value = line.substr(pos + 2); // skip ": "
    apr_table_add(r->headers_out, header.c_str(), value.c_str());
    ap_log_rerror(APLOG_MARK, APLOG_INFO, APR_SUCCESS, r,
                 "%s: %s", header.c_str(), value.c_str());
  }
  ap_log_rerror(APLOG_MARK, APLOG_INFO, APR_SUCCESS, r,
               "Sending content %d bytes", size);

  // send the body
  ap_rwrite(header, size, r);

  delete[] buffer;
  return OK;

}

std::string get_request_filename(request_rec* r) {
  // convert URI to filename
  std::string filename;

  char buffer[5] = {0};
  for (const char* uri = r->unparsed_uri; *uri; ++uri) {
    if ((*uri >= '0' && *uri <= '9') ||
        (*uri >= 'A' && *uri <= 'Z') ||
        (*uri >= 'a' && *uri <= 'z') ||
        (*uri == '_') ||
        (*uri == '/')) {
      filename.append(1, *uri);
    } else {
      snprintf(buffer, 4, "x%X", *uri);
      filename.append(buffer);
    }
  }

  if (filename.at(filename.size() - 1) == '/') {
    filename.append("indexx2Ehtml");
  }
  // TODO use ServerRoot
  std::string full_filename("/usr/local/apache2/htdocs/static/");
  full_filename.append(r->hostname);
  full_filename.append(filename);
  return full_filename;
}

int static_handler(request_rec* r) {
  // check if request for static
  if (!r->handler || strcmp(r->handler, "static")) {
    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, APR_SUCCESS, r,
                  "Not static request.");
    return DECLINED;
  }

  // Only handle GET request
  if (r->method_number != M_GET) {
    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, APR_SUCCESS, r,
                  "Not GET request: %d.", r->method_number);
    return HTTP_METHOD_NOT_ALLOWED;
  }

  // TODO remove DEBUG info
  ap_log_rerror(APLOG_MARK, APLOG_INFO, APR_SUCCESS, r,
                  "URI(unparsed): %s", r->unparsed_uri);
  ap_log_rerror(APLOG_MARK, APLOG_INFO, APR_SUCCESS, r,
                  "URI: %s", r->uri);
  ap_log_rerror(APLOG_MARK, APLOG_INFO, APR_SUCCESS, r,
                  "hostname: %s", r->hostname);
  ap_log_rerror(APLOG_MARK, APLOG_INFO, APR_SUCCESS, r,
                  "filename: %s", r->filename);

  std::string full_filename = get_request_filename(r);
  ap_log_rerror(APLOG_MARK, APLOG_INFO, APR_SUCCESS, r,
                  "full_filename: %s", full_filename.c_str());


  return process_file(full_filename, r);
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
