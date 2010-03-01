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

#include "mod_spdy/apache/input_filter_input_stream.h"

#include "base/logging.h"
#include "third_party/apache_httpd/include/apr_buckets.h"
#include "third_party/apache_httpd/include/util_filter.h"

namespace mod_spdy {

InputFilterInputStream::InputFilterInputStream(apr_pool_t *pool,
                                               apr_bucket_alloc_t *bucket_alloc)
    : filter_(NULL),
      brigade_(apr_brigade_create(pool, bucket_alloc)),
      tmp_brigade_(apr_brigade_create(pool, bucket_alloc)),
      block_(APR_NONBLOCK_READ),
      next_filter_rv_(APR_SUCCESS) {
}

InputFilterInputStream::~InputFilterInputStream() {
  apr_brigade_destroy(brigade_);
  apr_brigade_destroy(tmp_brigade_);
}

apr_status_t InputFilterInputStream::PullBytesFromNextFilter(
    size_t num_bytes) {
  apr_status_t rv = APR_SUCCESS;
  apr_status_t next_filter_rv = APR_SUCCESS;

  // NOTE: apr_brigade_length can be expensive for certain bucket types.
  // Revisit if this turns out to be a perf problem.
  apr_off_t brigade_len = 0;
  rv = apr_brigade_length(brigade_, 1, &brigade_len);
  if (rv != APR_SUCCESS) {
    return rv;
  }

  const apr_off_t data_needed = num_bytes - brigade_len;
  if (data_needed <= 0) {
    // We can satisfy the request, so stop reading from the filter chain.
    return APR_SUCCESS;
  }

  if (!APR_BRIGADE_EMPTY(tmp_brigade_)) {
    DCHECK(false);
    apr_brigade_cleanup(tmp_brigade_);
  }
  rv = ap_get_brigade(filter_->next,
                      tmp_brigade_,
                      AP_MODE_READBYTES,
                      block_,
                      data_needed);

  // Unfortunately we need to persist this value so it can later be
  // passed on to downstream filters. Ideally we would return this
  // value to the caller synchronously. Consider refactoring the
  // current API to make it unnecessary to persist this value as a
  // member.
  next_filter_rv_ = rv;

  // TODO: only concat data buckets? What about EOF, etc?
  APR_BRIGADE_CONCAT(brigade_, tmp_brigade_);
  return rv;
}

size_t InputFilterInputStream::Read(char *data, size_t data_len) {
  // We don't look at the return from PullBytesFromNextFilter. We just
  // want to pass any bytes that might already be in the buffer on to
  // to caller. The caller will find out about any errors encountered
  // by inspecting next_filter_rv().
  PullBytesFromNextFilter(data_len);

  apr_bucket *extra = NULL;
  apr_size_t bytes_read = FinishRead(data, data_len, &extra);
  for (apr_bucket *bucket = APR_BRIGADE_FIRST(brigade_);
       bucket != APR_BRIGADE_SENTINEL(brigade_);
       bucket = APR_BUCKET_NEXT(bucket)) {
    if (APR_BUCKET_IS_METADATA(bucket)) {
      // We should be passing all metadata buckets along to the next
      // filter.
      LOG(WARNING) << "SpdyInputFilter ignoring metadata bucket: "
                   << bucket->type != NULL ? bucket->type->name : "NULL";
    }
  }
  apr_brigade_cleanup(brigade_);
  if (extra != NULL) {
    APR_BRIGADE_INSERT_TAIL(brigade_, extra);
  }

  return bytes_read;
}

apr_size_t InputFilterInputStream::FinishRead(char *data,
                                              size_t data_len,
                                              apr_bucket **extra) {
  apr_off_t brigade_len = 0;
  apr_status_t rv = apr_brigade_length(brigade_, 1, &brigade_len);
  if (rv != APR_SUCCESS) {
    return 0;
  }

  if (brigade_len > data_len) {
    rv = apr_brigade_partition(brigade_, data_len, extra);
    if (rv != APR_SUCCESS) {
      return 0;
    }
  }

  apr_size_t bytes_read = data_len;
  rv = apr_brigade_flatten(brigade_, data, &bytes_read);
  if (rv != APR_SUCCESS) {
    return 0;
  }
  return bytes_read;
}

bool InputFilterInputStream::IsEmpty() const {
  if (!APR_BRIGADE_EMPTY(tmp_brigade_)) {
    DCHECK(false);
    apr_brigade_cleanup(tmp_brigade_);
  }

  apr_off_t brigade_len = 0;
  apr_status_t rv = apr_brigade_length(brigade_, 1, &brigade_len);
  if (rv != APR_SUCCESS) {
    return true;
  }

  return brigade_len == 0;
}

}  // namespace mod_spdy
