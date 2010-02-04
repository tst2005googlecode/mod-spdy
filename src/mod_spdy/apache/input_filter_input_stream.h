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

#ifndef MOD_SPDY_INPUT_FILTER_INPUT_STREAM_H_
#define MOD_SPDY_INPUT_FILTER_INPUT_STREAM_H_

#include "mod_spdy/common/input_stream_interface.h"

#include "third_party/apache_httpd/include/apr_buckets.h"
#include "third_party/apache_httpd/include/util_filter.h"

namespace mod_spdy {

// InputFilterInputStream provides an InputStreamInterface API
// to an apr_filter_t input filter.
class InputFilterInputStream : public InputStreamInterface {
 public:
  InputFilterInputStream(apr_pool_t *pool, apr_bucket_alloc_t *bucket_alloc);
  virtual ~InputFilterInputStream();

  virtual size_t Read(char *data, size_t data_len);

  void set_filter(ap_filter_t *filter, apr_read_type_e block) {
    filter_ = filter;
    block_ = block;
  }

  void clear_filter() {
    filter_ = NULL;
    block_ = APR_NONBLOCK_READ;
  }

  apr_status_t next_filter_rv() const { return next_filter_rv_; }

  // Is the internal buffer empty?
  bool IsEmpty() const;

 private:
  apr_status_t PullBytesFromNextFilter(size_t num_bytes);
  apr_size_t FinishRead(char *data, size_t data_len, apr_bucket **extra);

  ap_filter_t *filter_;
  apr_bucket_brigade *brigade_;
  apr_bucket_brigade *tmp_brigade_;
  apr_read_type_e block_;
  apr_status_t next_filter_rv_;
};

}  // namespace mod_spdy

#endif  // MOD_SPDY_INPUT_FILTER_INPUT_STREAM_H_
