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

#ifndef MOD_SPDY_HTTP_STREAM_ACCUMULATOR_H_
#define MOD_SPDY_HTTP_STREAM_ACCUMULATOR_H_

#include "mod_spdy/common/http_stream_visitor_interface.h"

#include "apr_buckets.h"
#include "util_filter.h"

namespace mod_spdy {

// HttpStreamAccumulator accumulates an HTTP request stream in its
// internal bucket brigade. The brigade can then be consumed via the
// Read() method.
class HttpStreamAccumulator : public HttpStreamVisitorInterface {
 public:
  HttpStreamAccumulator(apr_pool_t *pool, apr_bucket_alloc_t *bucket_alloc);
  virtual ~HttpStreamAccumulator();

  // Implement HttpStreamVisitorInterface
  virtual void OnStatusLine(const char *method,
                            const char *url,
                            const char *version);
  virtual void OnHeader(const char *key, const char *value);
  virtual void OnHeadersComplete();
  virtual void OnBody(const char *data, size_t data_len);

  // Is the internal buffer empty?
  bool IsEmpty() const;

  // Read data from the internal buffer via an
  // ap_in_filter_func-like interface.
  apr_status_t Read(apr_bucket_brigade *brigade,
                    ap_input_mode_t mode,
                    apr_read_type_e block,
                    apr_off_t readbytes);

 private:
  apr_pool_t *const pool_;
  apr_bucket_alloc_t *const bucket_alloc_;
  apr_bucket_brigade *brigade_;
};

}  // namespace mod_spdy

#endif  // MOD_SPDY_HTTP_STREAM_ACCUMULATOR_H_
