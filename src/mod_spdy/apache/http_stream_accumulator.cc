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
#include "mod_spdy/apache/pool_util.h"
#include "third_party/apache/aprutil/src/include/apr_uri.h"

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
  if (bytes_written != bufsize) {
    return false;
  }

  return true;
}

}  // namespace

namespace mod_spdy {

HttpStreamAccumulator::HttpStreamAccumulator(
    apr_pool_t *pool, apr_bucket_alloc_t *bucket_alloc)
    : pool_(pool),
      bucket_alloc_(bucket_alloc),
      brigade_(apr_brigade_create(pool_, bucket_alloc_)),
      is_complete_(false),
      error_(false) {
}

HttpStreamAccumulator::~HttpStreamAccumulator() {
  apr_brigade_destroy(brigade_);
}

void HttpStreamAccumulator::OnStatusLine(const char *method,
                                         const char *url,
                                         const char *version) {
  if (error_) {
    DCHECK(false);
    return;
  }

  if (is_complete_) {
    DCHECK(false);
    error_ = true;
    return;
  }

  LocalPool local;
  if (local.status() != APR_SUCCESS) {
    DCHECK(false);
    error_ = true;
    return;
  }

  apr_uri_t uri;
  apr_status_t rv = apr_uri_parse(local.pool(), url, &uri);
  if (rv != APR_SUCCESS) {
    DCHECK(false);
    error_ = true;
    return;
  }
  if (strlen(uri.hostname) <= 0) {
    DCHECK(false);
    error_ = true;
    return;
  }

  // We are using the full URL in the request line
  // instead of the path for compatibility with
  // mod_proxy (which requires the full URL).  This
  // approach appears to work fine even when mod_proxy
  // is not enabled, but there might be unintended
  // consequences.
  const size_t status_line_bufsize =
      strlen(method) +
      kSpaceLen +
      strlen(url) +
      kSpaceLen +
      strlen(version) +
      kCRLFLen;

  if (!FormatAndAppend(brigade_,
                       status_line_bufsize,
                       "%s %s %s%s",
                       method,
                       url,
                       version,
                       kCRLF)) {
    DCHECK(false);
    error_ = true;
    return;
  }

  OnHeader(kHost, uri.hostname);
}

void HttpStreamAccumulator::OnHeader(const char *key, const char *value) {
  if (error_) {
    DCHECK(false);
    return;
  }

  if (is_complete_) {
    DCHECK(false);
    error_ = true;
    return;
  }

  const size_t header_line_bufsize =
      strlen(key) +
      kHeaderSeparatorLen +
      strlen(value) +
      kCRLFLen;

  if (!FormatAndAppend(brigade_,
                       header_line_bufsize,
                       "%s%s%s%s",
                       key,
                       kHeaderSeparator,
                       value,
                       kCRLF)) {
    DCHECK(false);
    error_ = true;
  }
}

void HttpStreamAccumulator::OnHeadersComplete() {
  if (error_) {
    DCHECK(false);
    return;
  }

  if (is_complete_) {
    DCHECK(false);
    error_ = true;
    return;
  }

  if (!FormatAndAppend(brigade_,
                       kCRLFLen,
                       "%s",
                       kCRLF)) {
    DCHECK(false);
    error_ = true;
  }
}

void HttpStreamAccumulator::OnBody(const char *data, size_t data_len) {
  if (error_) {
    DCHECK(false);
    return;
  }

  if (is_complete_) {
    DCHECK(false);
    error_ = true;
    return;
  }

  APR_BRIGADE_INSERT_TAIL(brigade_,
                          apr_bucket_heap_create(data,
                                                 data_len,
                                                 NULL,
                                                 brigade_->bucket_alloc));
}

void HttpStreamAccumulator::OnComplete() {
  if (error_) {
    DCHECK(false);
    return;
  }

  if (is_complete_) {
    DCHECK(false);
    error_ = true;
    return;
  }

  is_complete_ = true;
}

void HttpStreamAccumulator::OnTerminate() {
  is_complete_ = true;
  error_ = true;
}

bool HttpStreamAccumulator::IsEmpty() const {
  if (error_) {
    DCHECK(false);
    return true;
  }

  if (brigade_ == NULL) {
    return true;
  }

  apr_off_t brigade_len = 0;
  apr_status_t rv = apr_brigade_length(brigade_, 1, &brigade_len);
  if (rv != APR_SUCCESS) {
    return true;
  }

  return brigade_len == 0;
}

apr_status_t HttpStreamAccumulator::Read(apr_bucket_brigade *dest,
                                         ap_input_mode_t mode,
                                         apr_read_type_e block,
                                         apr_off_t readbytes) {
  if (error_) {
    DCHECK(false);
    return APR_EGENERAL;
  }

  // From ap_core_input_filter().
  if (mode == AP_MODE_INIT) {
    return APR_SUCCESS;
  }

  if (IsEmpty()) {
    if (mode == AP_MODE_READBYTES ||
        mode == AP_MODE_GETLINE) {
      if (block == APR_NONBLOCK_READ) {
        return APR_EAGAIN;
      } else {
        return APR_SUCCESS;
      }
    }
  }

  apr_status_t rv = APR_SUCCESS;
  apr_bucket *extra = NULL;
  switch(mode) {
    case AP_MODE_GETLINE:
      // ap_core_input_filter maps APR_EAGAIN to APR_SUCCESS in this
      // mode, so we don't have to do anything special here since we
      // expect this method to return APR_SUCCESS (see comment below).
      rv = apr_brigade_split_line(dest, brigade_, block, HUGE_STRING_LEN);

      // apr_brigade_split_line only returns non-APR_SUCCESS if a
      // bucket read fails, so this should only return APR_SUCCESS
      // since we only use HEAP buckets, and reading from HEAP buckets
      // should never fail.
      DCHECK(rv == APR_SUCCESS);
      break;

    case AP_MODE_READBYTES:
      DCHECK(readbytes > 0);
      rv = apr_brigade_partition(brigade_, readbytes, &extra);
      if (rv == APR_INCOMPLETE) {
        // If readbytes is greater than the number of bytes in the
        // brigade_, we get APR_INCOMPLETE. Map that to APR_SUCCESS
        // since ap_core_input_filter() doesn't return APR_INCOMPLETE.
        rv = APR_SUCCESS;
      } else if (rv != APR_SUCCESS) {
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

      // In this case, ap_core_input_filter() will return the status
      // code from the call to apr_bucket_read(), which is APR_EAGAIN
      // in non-blocking mode, so we do the same.
      if (block == APR_NONBLOCK_READ) {
        rv = APR_EAGAIN;
      }
      break;

    default:
      // Not supported. TODO: must add support for other modes.
      CHECK(false);
  }

  return rv;
}

}  //namespace mod_spdy
