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

#ifndef MOD_SPDY_HTTP_STREAM_VISITOR_INTERFACE_H_
#define MOD_SPDY_HTTP_STREAM_VISITOR_INTERFACE_H_

#include <stddef.h>  // for size_t

namespace mod_spdy {

// Interface that gets called back as an HTTP stream is visited.
class HttpStreamVisitorInterface {
 public:
  HttpStreamVisitorInterface();
  virtual ~HttpStreamVisitorInterface();

  // Called when an HTTP status line is visited. Indicates that a new
  // HTTP request is being visited.
  virtual void OnStatusLine(const char *method,
                            const char *url,
                            const char *version) = 0;

  // Called zero or more times, after OnStatusLine, once for each HTTP
  // header.
  virtual void OnHeader(const char *key, const char *value) = 0;

  // Called once, after all HTTP headers have been visited.
  virtual void OnHeadersComplete() = 0;

  // Called zero or more times, after OnHeadersComplete, once for each
  // "chunk" of the HTTP body. Chunks are delivered sequentially.
  virtual void OnBody(const char *data, size_t data_len) = 0;
};

}  // namespace mod_spdy

#endif  // MOD_SPDY_HTTP_STREAM_VISITOR_INTERFACE_H_
