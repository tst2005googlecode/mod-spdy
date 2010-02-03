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

#ifndef MOD_SPDY_APACHE_SPDY_OUTPUT_FILTER_H_
#define MOD_SPDY_APACHE_SPDY_OUTPUT_FILTER_H_

#include "base/basictypes.h"
#include "base/scoped_ptr.h"
#include "third_party/apache_httpd/include/apr_buckets.h"
#include "third_party/apache_httpd/include/util_filter.h"

namespace mod_spdy {

class ConnectionContext;
class OutputFilterContext;

class SpdyOutputFilter {
 public:
  explicit SpdyOutputFilter(ConnectionContext* conn_context);
  ~SpdyOutputFilter();

  // Read data from the given brigade and write the result through the given
  // filter. This method is responsible for driving the HTTP to SPDY conversion
  // process.
  apr_status_t Write(ap_filter_t* filter,
                     apr_bucket_brigade* input_brigade);

 private:
  scoped_ptr<OutputFilterContext> context_;

  DISALLOW_COPY_AND_ASSIGN(SpdyOutputFilter);
};

}  // namespace mod_spdy

#endif  // MOD_SPDY_APACHE_SPDY_OUTPUT_FILTER_H_
