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

#include "net/spdy/spdy_framer.h"
#include "net/spdy/spdy_protocol.h"

// For some reason, spdy_protocol.h doesn't define kScheme along with kMethod,
// kVersion, etc.  So we add it ourselves:
namespace spdy {
extern const char* const kScheme;
}  // namespace spdy

namespace mod_spdy {

namespace http {

// HTTP header names.  These values are all lower-case, so they can be used
// directly in SPDY header blocks.
extern const char* const kConnection;
extern const char* const kContentLength;
extern const char* const kContentType;
extern const char* const kHost;
extern const char* const kKeepAlive;
extern const char* const kTransferEncoding;
extern const char* const kXModSpdy;

// HTTP header values.
extern const char* const kChunked;

}  // namespace http

// Given a pointer to (uncompressed) SPDY header block data, parse out the
// headers into a SpdyHeaderBlock map.  Return false on failure.
// TODO(mdsteele): In more recent versions of net/spdy/, this is a static
//   method on SpdyFramer.  We should upgrade and use that instead of
//   duplicating it here.
bool ParseHeaderBlockInBuffer(const char* header_data,
                              size_t header_length,
                              spdy::SpdyHeaderBlock* block);

}  // namespace mod_spdy

#endif  // MOD_SPDY_COMMON_PROTOCOL_UTIL_H_
