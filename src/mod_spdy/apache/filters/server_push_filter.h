// Copyright 2012 Google Inc.
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

#ifndef MOD_SPDY_APACHE_FILTERS_SERVER_PUSH_FILTER_H_
#define MOD_SPDY_APACHE_FILTERS_SERVER_PUSH_FILTER_H_

#include <string>

#include "apr_buckets.h"
#include "util_filter.h"

#include "base/basictypes.h"

namespace mod_spdy {

class SpdyStream;

// An Apache filter for initiating SPDY server pushes based on the
// X-Associated-Content response header, which may be set by e.g. mod_headers
// or a CGI script.
class ServerPushFilter {
 public:
  explicit ServerPushFilter(SpdyStream* stream);
  ~ServerPushFilter();

  // Read data from the given brigade and write the result through the given
  // filter.  This filter doesn't touch the data; it only looks at the request
  // headers.
  apr_status_t Write(ap_filter_t* filter, apr_bucket_brigade* input_brigade);

 private:
  SpdyStream* stream_;

  DISALLOW_COPY_AND_ASSIGN(ServerPushFilter);
};

}  // namespace mod_spdy

#endif  // MOD_SPDY_APACHE_FILTERS_SERVER_PUSH_FILTER_H_
