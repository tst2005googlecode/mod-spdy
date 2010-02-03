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

#include "mod_spdy/apache/http_stream_accumulator.h"

#include "base/compiler_specific.h"  // for PRINTF_FORMAT()
#include "base/logging.h"
#include "apr_uri.h"

namespace {

const char *kDefaultPath = "/";
const char *kCRLF = "\r\n";
const char *kHeaderSeparator = ": ";
const char *kHost = "Host";

const size_t kCRLFLen = strlen(kCRLF);
const size_t kHeaderSeparatorLen = strlen(kHeaderSeparator);
const size_t kSpaceLen = 1;

bool FormatAndAppend(apr_bucket_brigade *brigade,
                     const size_t bufsize,
                     const char *format,
                     ...) PRINTF_FORMAT(3, 4);

bool FormatAndAppend(apr_bucket_brigade *brigade,
                     const size_t bufsize,
                     const char *format,
                     ...) {
  // NOTE: comparing brigade length before and after is potentially
  // expensive.
  apr_off_t brigade_len_before = 0;
  apr_status_t rv = apr_brigade_length(brigade, 1, &brigade_len_before);
  if (rv != APR_SUCCESS) {
    return false;
  }

  va_list arguments;
  va_start(arguments, format);
  rv = apr_brigade_vprintf(brigade, NULL, NULL, format, arguments);
  va_end(arguments);

  if (rv != APR_SUCCESS) {
    return false;
  }

  apr_off_t brigade_len_after = 0;
  rv = apr_brigade_length(brigade, 1, &brigade_len_after);
  if (rv != APR_SUCCESS) {
    return false;
  }

  const apr_off_t bytes_written = brigade_len_after - brigade_len_before;
  CHECK(bytes_written == bufsize);

  return true;
}

}  // namespace

namespace mod_spdy {

HttpStreamAccumulator::HttpStreamAccumulator(
    apr_pool_t *pool, apr_bucket_alloc_t *bucket_alloc)
    : pool_(pool),
      bucket_alloc_(bucket_alloc),
      brigade_(apr_brigade_create(pool_, bucket_alloc_)) {
}

HttpStreamAccumulator::~HttpStreamAccumulator() {
  apr_brigade_destroy(brigade_);
}

void HttpStreamAccumulator::OnStatusLine(const char *method,
                                         const char *url,
                                         const char *version) {
  // TODO: use something other than apr_uri_t since there is no way to
  // explicitly free it (and thus it sits in the connection's memory
  // pool for the lifetime of the connection).
  apr_uri_t uri;
  apr_status_t rv = apr_uri_parse(pool_, url, &uri);
  CHECK(rv == APR_SUCCESS);
  const char *path = uri.path;
  if (path == NULL || path[0] == '\0') {
    path = kDefaultPath;
  }

  const size_t status_line_bufsize =
      strlen(method) +
      kSpaceLen +
      strlen(path) +
      kSpaceLen +
      strlen(version) +
      kCRLFLen;

  FormatAndAppend(brigade_,
                  status_line_bufsize,
                  "%s %s %s%s",
                  method,
                  path,
                  version,
                  kCRLF);

  CHECK(strlen(uri.hostname) > 0);
  OnHeader(kHost, uri.hostname);
}

void HttpStreamAccumulator::OnHeader(const char *key, const char *value) {
  const size_t header_line_bufsize =
      strlen(key) +
      kHeaderSeparatorLen +
      strlen(value) +
      kCRLFLen;

  FormatAndAppend(brigade_,
                  header_line_bufsize,
                  "%s%s%s%s",
                  key,
                  kHeaderSeparator,
                  value,
                  kCRLF);
}

void HttpStreamAccumulator::OnHeadersComplete() {
  APR_BRIGADE_INSERT_TAIL(brigade_,
                          apr_bucket_immortal_create(kCRLF,
                                                     kCRLFLen,
                                                     brigade_->bucket_alloc));
}

void HttpStreamAccumulator::OnBody(const char *data, size_t data_len) {
  CHECK(false);
}

bool HttpStreamAccumulator::IsEmpty() const {
  if (brigade_ == NULL) {
    return true;
  }

  // TODO: what if brigade contains non-data buckets (e.g. EOF)?
  return APR_BRIGADE_EMPTY(brigade_);
}

apr_status_t HttpStreamAccumulator::Read(apr_bucket_brigade *dest,
                                         ap_input_mode_t mode,
                                         apr_read_type_e block,
                                         apr_off_t readbytes) {
  if (IsEmpty()) {
    return APR_EOF;
  }
  apr_status_t rv = APR_SUCCESS;
  apr_bucket *extra = NULL;
  switch(mode) {
    case AP_MODE_GETLINE:
      rv = apr_brigade_split_line(dest, brigade_, block, HUGE_STRING_LEN);
      break;

    case AP_MODE_READBYTES:
      rv = apr_brigade_partition(brigade_, readbytes, &extra);
      if (rv != APR_SUCCESS) {
        return rv;
      }
      APR_BRIGADE_CONCAT(dest, brigade_);
      if (extra != NULL) {
        APR_BRIGADE_INSERT_TAIL(brigade_, extra);
      }
      break;

    case AP_MODE_EATCRLF:
      // TODO: Strip leading CRLF pairs from the input see
      // core_filters.c for impl idea.
      break;

    default:
      // Not supported
      CHECK(false);
  }

  return rv;
}

}  //namespace mod_spdy
