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

#ifndef MOD_SPDY_APACHE_FILTERS_HTTP_TO_SPDY_FILTER_H_
#define MOD_SPDY_APACHE_FILTERS_HTTP_TO_SPDY_FILTER_H_

#include <string>

#include "apr_buckets.h"
#include "util_filter.h"

#include "base/basictypes.h"
#include "net/spdy/spdy_framer.h"

namespace mod_spdy {

class HeaderPopulatorInterface;
class SpdyStream;

// An Apache filter for converting HTTP data into SPDY frames and sending them
// to the output queue of a SpdyStream object.  This is intended to be the last
// filter in the output chain of one of our slave connections.
class HttpToSpdyFilter {
 public:
  explicit HttpToSpdyFilter(SpdyStream* stream);
  ~HttpToSpdyFilter();

  // Read data from the given brigade and write the result through the given
  // filter. This method is responsible for driving the HTTP to SPDY conversion
  // process.
  apr_status_t Write(ap_filter_t* filter, apr_bucket_brigade* input_brigade);

 private:
  // Send SPDY frames for headers and/or data, as necessary.  If the flush
  // argument is true (or if the end-of-stream has been reached), send _all_
  // data from the buffer rather than waiting for a "full" data frame.
  void Send(ap_filter_t* filter, bool flush);

  void SendHeaders(const HeaderPopulatorInterface& populator, bool flag_fin);
  void SendData(const char* data, size_t size, bool flag_fin);

  SpdyStream* const stream_;
  std::string data_buffer_;
  bool headers_have_been_sent_;
  bool end_of_stream_reached_;

  DISALLOW_COPY_AND_ASSIGN(HttpToSpdyFilter);
};

}  // namespace mod_spdy

#endif  // MOD_SPDY_APACHE_FILTERS_HTTP_TO_SPDY_FILTER_H_
