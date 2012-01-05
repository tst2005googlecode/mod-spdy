// Copyright 2011 Google Inc.
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

#ifndef MOD_SPDY_APACHE_FILTERS_SPDY_TO_HTTP_FILTER_H_
#define MOD_SPDY_APACHE_FILTERS_SPDY_TO_HTTP_FILTER_H_

#include <string>

#include "apr_buckets.h"
#include "util_filter.h"

#include "base/basictypes.h"
#include "net/spdy/spdy_protocol.h"

namespace mod_spdy {

class SpdyStream;

// An Apache filter for pulling SPDY frames (for a single SPDY stream) from the
// input queue of a SpdyStream object and converting them into equivalent HTTP
// data to be processed by Apache.  This is intended to be the outermost filter
// in the input chain of one of our slave connections, essentially taking the
// place of the network socket.
class SpdyToHttpFilter {
 public:
  explicit SpdyToHttpFilter(SpdyStream* stream);
  ~SpdyToHttpFilter();

  apr_status_t Read(ap_filter_t* filter,
                    apr_bucket_brigade* brigade,
                    ap_input_mode_t mode,
                    apr_read_type_e block,
                    apr_off_t readbytes);

 private:
  // Try to get the next SPDY frame on this stream, convert it into HTTP, and
  // append the resulting data to data_buffer_.  If the block argument is
  // APR_BLOCK_READ, this function will block until a frame comes in (or the
  // stream is closed).
  bool GetNextFrame(apr_read_type_e block);

  // Translate a SYN_STREAM frame to HTTP and append it to data_buffer_.
  void DecodeSynStreamFrame(const spdy::SpdySynStreamControlFrame& frame);
  // Translate a HEADERS frame to HTTP and buffer it in trailing_headers_, to
  // be appended to data_buffer_ at the end of the data payload.
  void DecodeHeadersFrame(const spdy::SpdyHeadersControlFrame& frame);
  // Append the contents of a data frame to data_buffer_.
  void DecodeDataFrame(const spdy::SpdyDataFrame& frame);

  // Called when we see a FLAG_FIN.  This terminates the request and appends
  // whatever trailing headers (if any) we have buffered.
  void FinishRequest();

  // Send a RST_STREAM frame and abort the stream.
  void AbortStream(spdy::SpdyStatusCodes status);

  SpdyStream* const stream_;
  std::string trailing_headers_;
  std::string data_buffer_;
  int next_read_start_;
  bool received_any_data_frames_yet_;
  bool end_of_stream_reached_;

  DISALLOW_COPY_AND_ASSIGN(SpdyToHttpFilter);
};

}  // namespace mod_spdy

#endif  // MOD_SPDY_APACHE_FILTERS_SPDY_TO_HTTP_FILTER_H_
