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

#ifndef MOD_SPDY_COMMON_PROTOCOL_UTIL_H_
#define MOD_SPDY_COMMON_PROTOCOL_UTIL_H_

#include "mod_spdy/common/protocol_util.h"

#include "net/spdy/spdy_frame_builder.h"
#include "net/spdy/spdy_framer.h"
#include "net/spdy/spdy_protocol.h"

// For some reason, spdy_protocol.h doesn't define kScheme along with kMethod,
// kVersion, etc.  So we add it ourselves:
namespace spdy {
extern const char* const kScheme = "scheme";
}  // namespace spdy

namespace mod_spdy {

namespace http {

extern const char* const kConnection = "connection";
extern const char* const kContentLength = "content-length";
extern const char* const kContentType = "content-type";
extern const char* const kHost = "host";
extern const char* const kKeepAlive = "keep-alive";
extern const char* const kTransferEncoding = "transfer-encoding";
extern const char* const kXModSpdy = "x-mod-spdy";

extern const char* const kChunked = "chunked";

}  // namespace http

bool ParseHeaderBlockInBuffer(const char* header_data,
                              size_t header_length,
                              spdy::SpdyHeaderBlock* block) {
  // Code from spdy_framer.cc:
  spdy::SpdyFrameBuilder builder(header_data, header_length);
  void* iter = NULL;
  uint16 num_headers;
  if (builder.ReadUInt16(&iter, &num_headers)) {
    int index;
    for (index = 0; index < num_headers; ++index) {
      std::string name;
      std::string value;
      if (!builder.ReadString(&iter, &name))
        break;
      if (!builder.ReadString(&iter, &value))
        break;
      if (!name.size() || !value.size())
        return false;
      if (block->find(name) == block->end()) {
        (*block)[name] = value;
      } else {
        return false;
      }
    }
    return index == num_headers &&
        iter == header_data + header_length;
  }
  return false;
}

}  // namespace mod_spdy

#endif  // MOD_SPDY_COMMON_PROTOCOL_UTIL_H_
