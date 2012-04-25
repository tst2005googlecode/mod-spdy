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

#include "base/string_piece.h"
#include "net/spdy/spdy_framer.h"
#include "net/spdy/spdy_protocol.h"

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

namespace spdy {

// Magic header names for SPDY v2.
// TODO(mdsteele): These values are different for SPDY v3.  Once we support
//   SPDY v3, we'll probably want to rename these, or provide functions that
//   return values based on a version argument, or whatever.
extern const char* const kMethod;
extern const char* const kScheme;
extern const char* const kStatus;
extern const char* const kUrl;
extern const char* const kVersion;

}  // namespace spdy

// Return a view of the raw bytes of the frame.
base::StringPiece FrameData(const net::SpdyFrame& frame);

}  // namespace mod_spdy

#endif  // MOD_SPDY_COMMON_PROTOCOL_UTIL_H_
