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

#ifndef MOD_SPDY_OUTPUT_FILTER_CONTEXT_H_
#define MOD_SPDY_OUTPUT_FILTER_CONTEXT_H_

#include "base/basictypes.h"

namespace mod_spdy {

class HeaderPopulatorInterface;
class OutputStreamInterface;
class ConnectionContext;

// Context object for the SPDY output filter.
class OutputFilterContext {
 public:
  explicit OutputFilterContext(ConnectionContext* conn_context);
  ~OutputFilterContext();

  /**
   * Convert HTTP headers to SPDY and send them.
   *
   * @param populator an object that can provide the HTTP headers
   * @param is_end_of_stream true iff there is no body data after the headers
   * @param output_stream the stream to which SPDY data should be written
   * @return true iff we were successful
   */
  bool SendHeaders(const HeaderPopulatorInterface& populator,
                   bool is_end_of_stream,
                   OutputStreamInterface* output_stream);

  /**
   * Convert a fragment of an HTTP response body into SPDY.
   *
   * @param input_data the HTTP data to convert
   * @param input_size the length of the HTTP data
   * @param is_end_of_stream true iff this fragment is the end of the stream
   * @param output_stream the stream to which SPDY data should be written
   * @return true iff we were successful
   */
  bool SendData(const char* input_data, size_t input_size,
                bool is_end_of_stream,
                mod_spdy::OutputStreamInterface* output_stream);

  /**
   * @return true iff SendHeaders has been called before
   */
  bool headers_have_been_sent() const { return headers_have_been_sent_; }

 private:
  ConnectionContext* const conn_context_;  // non-owning pointer
  bool headers_have_been_sent_;

  DISALLOW_COPY_AND_ASSIGN(OutputFilterContext);
};

}  // namespace mod_spdy

#endif  // MOD_SPDY_OUTPUT_FILTER_CONTEXT_H_
