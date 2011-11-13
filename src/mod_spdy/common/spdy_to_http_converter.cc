// Copyright 2010 Google Inc. All Rights Reserved.
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

#include "mod_spdy/common/spdy_to_http_converter.h"

#include "base/string_number_conversions.h"  // for Int64ToString

#include "mod_spdy/common/http_stream_visitor_interface.h"

namespace {

const char *kMethod = "method";
const char *kScheme = "scheme";
const char *kHost = "host";
const char *kPath = "url";  // Chromium currently uses "url" instead of "path".
const char *kVersion = "version";
const char *kConnection = "connection";
const char *kKeepAlive = "keep-alive";

}  // namespace

namespace mod_spdy {

SpdyToHttpConverter::SpdyToHttpConverter(spdy::SpdyFramer *framer,
                                         HttpStreamVisitorInterface *visitor)
    : framer_(framer), visitor_(visitor), error_(false) {
}

SpdyToHttpConverter::~SpdyToHttpConverter() {}

void SpdyToHttpConverter::OnError(spdy::SpdyFramer *framer) {
  DCHECK(false);
  visitor_->OnTerminate();
  error_ = true;
}

void SpdyToHttpConverter::OnControl(const spdy::SpdyControlFrame *frame) {
  if (HasError()) {
    return;
  }

  // For now we support a subset of SPDY. Log if we receive a frame
  // we don't yet know how to process.
  switch (frame->type()) {
    case spdy::SYN_STREAM:
      OnSynStream(static_cast<const spdy::SpdySynStreamControlFrame*>(frame));
      break;

    case spdy::RST_STREAM:
      visitor_->OnTerminate();
      break;

    case spdy::NOOP:
      // We're supposed to ignore NOOP frames.
      break;

    // We don't yet support the following frame types.
    case spdy::HEADERS:
    case spdy::SYN_REPLY:
    case spdy::SETTINGS:
    case spdy::PING:
    case spdy::GOAWAY:
      LOG(DFATAL) << "Received unsupported frame type: " << frame->type();
      OnError(framer_);
      break;

    default:
      LOG(DFATAL) << "Received unexpected frame type: " << frame->type();
      OnError(framer_);
      break;
  }
}

void SpdyToHttpConverter::
OnSynStream(const spdy::SpdySynStreamControlFrame *frame) {
  spdy::SpdyHeaderBlock block;
  if (!framer_->ParseHeaderBlock(frame, &block)) {
    LOG(DFATAL) << "Failed to parse header block.";
    OnError(framer_);
    return;
  }

  if (block.count(kMethod) != 1 ||
      block.count(kScheme) != 1 ||
      block.count(kHost) != 1 ||
      block.count(kPath) != 1 ||
      block.count(kVersion) != 1) {
    LOG(DFATAL) << "SynStream is missing required headers.";
    OnError(framer_);
    return;
  }

  // Technically we should decode the URL into a path and a
  // Host. Instead we pass the full URL on to the visitor and leave it
  // up to the visitor to extract Host and path.
  visitor_->OnStatusLine(block[kMethod].c_str(),
                         block[kScheme].c_str(),
                         block[kHost].c_str(),
                         block[kPath].c_str(),
                         block[kVersion].c_str());

  // Write the stream ID into a custom header, to be read back afterwards by
  // our output filter so that we know which stream to respond on.  We put this
  // header first in the list so that it won't be shadowed if the client sends
  // a header with the same name.
  //
  // TODO: This is sort of a hack; we probably want to find a better way to do
  //       this later.  Ideally, we would attach the stream ID directly to the
  //       request object (using its configuration vector; see TAMB 4.2.2.2),
  //       but our input filter runs before the request object has been
  //       created.  There are several possible solutions; we should probably
  //       revisit this issue once we figure out how multiplexing will work.
  const spdy::SpdyStreamId stream_id = frame->stream_id();
  const std::string stream_id_str(base::Int64ToString(stream_id));
  visitor_->OnHeader("x-spdy-stream-id", stream_id_str.c_str());

  // Write out the rest of the HTTP headers.
  for (spdy::SpdyHeaderBlock::const_iterator it = block.begin(),
           it_end = block.end();
       it != it_end;
       ++it) {
    std::string key = it->first;
    std::string value = it->second;
    if (key == kMethod ||
        key == kScheme ||
        key == kHost ||
        key == kPath ||
        key == kVersion) {
      // A SPDY-specific header. Do not emit it to the HttpStreamVisitorInterface.
      continue;
    }

    if (key == kConnection ||
        key == kKeepAlive) {
      // Skip headers that are ignored by SPDY.
      continue;
    }

    // Split header values on null characters, emitting a separate
    // header key-value pair for each substring. Logic from
    // net/spdy/spdy_session.cc
    for (size_t start = 0, end = 0; end != value.npos; start = end) {
      start = value.find_first_not_of('\0', start);
      if (start == value.npos) {
        break;
      }
      end = value.find('\0', start);
      std::string tval;
      if (end != value.npos) {
        tval = value.substr(start, (end - start));
      } else {
        tval = value.substr(start);
      }
      visitor_->OnHeader(key.c_str(), tval.c_str());
    }
  }

  // Explicitly set Keep-Alive on HTTP/1.0 requests
  // to prevent Apache from closing the socket
  if (block[kVersion] == "HTTP/1.0") {
    visitor_->OnHeader(kConnection, "Keep-Alive");
  }

  visitor_->OnHeadersComplete();

  if (frame->flags() & spdy::CONTROL_FLAG_FIN) {
    visitor_->OnComplete();
  }
}

void SpdyToHttpConverter::OnStreamFrameData(spdy::SpdyStreamId stream_id,
                                            const char *data,
                                            size_t len) {
  if (HasError()) {
    return;
  }

  if (len == 0) {
    visitor_->OnComplete();
  } else {
    visitor_->OnBody(data, len);
  }
}

}  // namespace mod_spdy
