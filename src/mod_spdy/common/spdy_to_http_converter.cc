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

#include "base/logging.h"
#include "base/string_number_conversions.h"  // for Int64ToString
#include "base/string_piece.h"
#include "mod_spdy/common/http_stream_visitor_interface.h"
#include "net/spdy/spdy_frame_builder.h"
#include "net/spdy/spdy_framer.h"
#include "net/spdy/spdy_protocol.h"

namespace {

// Header names:
const char* const kConnection = "connection";
const char* const kContentLength = "content-length";
const char* const kHost = "host";
const char* const kKeepAlive = "keep-alive";
const char* const kMethod = "method";
const char* const kPath = "url";  // Chromium uses "url" instead of "path".
const char* const kScheme = "scheme";
const char* const kTransferEncoding = "transfer-encoding";
const char* const kVersion = "version";
// Header values:
const char* const kChunked = "chunked";

// Functions to test for FLAG_FIN.  Using these functions instead of testing
// flags() directly helps guard against mixing up & with && or mixing up
// CONTROL_FLAG_FIN with DATA_FLAG_FIN.
bool HasControlFlagFinSet(const spdy::SpdyControlFrame& frame) {
  return bool(frame.flags() & spdy::CONTROL_FLAG_FIN);
}
bool HasDataFlagFinSet(const spdy::SpdyDataFrame& frame) {
  return bool(frame.flags() & spdy::DATA_FLAG_FIN);
}

// TODO(mdsteele): In more recent versions of net/spdy/, this is a static
//   method on SpdyFramer.  We should upgrade and use that instead of
//   duplicating it here.
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

// Generate an HTTP request line from the given SPDY header block by calling
// the OnStatusLine() method of the given visitor, and return true.  If there's
// an error, this will return false without calling any methods on the visitor.
bool GenerateRequestLine(const spdy::SpdyHeaderBlock& block,
                         mod_spdy::HttpStreamVisitorInterface* visitor) {
  spdy::SpdyHeaderBlock::const_iterator method = block.find(kMethod);
  spdy::SpdyHeaderBlock::const_iterator scheme = block.find(kScheme);
  spdy::SpdyHeaderBlock::const_iterator host = block.find(kHost);
  spdy::SpdyHeaderBlock::const_iterator path = block.find(kPath);
  spdy::SpdyHeaderBlock::const_iterator version = block.find(kVersion);

  if (method == block.end() ||
      scheme == block.end() ||
      host == block.end() ||
      path == block.end() ||
      version == block.end()) {
    return false;
  }

  visitor->OnRequestLine(method->second, path->second, version->second);
  return true;
}

// Convert the given SPDY header block (e.g. from a SYN_STREAM, SYN_REPLY, or
// HEADERS frame) into HTTP headers by calling the specified method (either
// OnLeadingHeader or OnTrailingHeader) of the given visitor.
template <void(mod_spdy::HttpStreamVisitorInterface::*OnHeader)(
    const base::StringPiece& key, const base::StringPiece& value)>
void GenerateHeaders(const spdy::SpdyHeaderBlock& block,
                     mod_spdy::HttpStreamVisitorInterface* visitor) {
  for (spdy::SpdyHeaderBlock::const_iterator it = block.begin();
       it != block.end(); ++it) {
    const base::StringPiece key = it->first;
    const base::StringPiece value = it->second;

    // Skip SPDY-specific (i.e. non-HTTP) headers.
    if (key == kMethod || key == kScheme || key == kPath || key == kVersion) {
      continue;
    }

    // Skip headers that are ignored by SPDY.
    if (key == kConnection || key == kKeepAlive) {
      continue;
    }

    // If the client sent a Content-Length header, ignore it.  We'd rather rely
    // on the SPDY framing layer to determine when a request is complete.
    if (key == kContentLength) {
      continue;
    }

    // The client shouldn't be sending us a Transfer-Encoding header; it's
    // pretty pointless over SPDY.  If they do send one, just ignore it; we may
    // be overriding it later anyway.
    if (key == kTransferEncoding) {
      LOG(WARNING) << "Client sent \"transfer-encoding: " << value
                   << "\" header over SPDY.  Why would they do that?";
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
      (visitor->*OnHeader)(key, (end != value.npos ?
                                 value.substr(start, (end - start)) :
                                 value.substr(start)));
    }
  }
}

}  // namespace

namespace mod_spdy {

SpdyToHttpConverter::SpdyToHttpConverter(HttpStreamVisitorInterface* visitor)
    : visitor_(visitor),
      state_(NO_FRAMES_YET) {
  CHECK(visitor);
}

SpdyToHttpConverter::~SpdyToHttpConverter() {}

// static
const char* SpdyToHttpConverter::StatusString(Status status) {
  switch (status) {
    case SPDY_CONVERTER_SUCCESS:  return "SPDY_CONVERTER_SUCCESS";
    case FRAME_BEFORE_SYN_STREAM: return "FRAME_BEFORE_SYN_STREAM";
    case FRAME_AFTER_FIN:         return "FRAME_AFTER_FIN";
    case EXTRA_SYN_STREAM:        return "EXTRA_SYN_STREAM";
    case INVALID_HEADER_BLOCK:    return "INVALID_HEADER_BLOCK";
    case BAD_REQUEST:             return "BAD_REQUEST";
    default:
      LOG(DFATAL) << "Invalid status value: " << status;
      return "???";
  }
}

SpdyToHttpConverter::Status SpdyToHttpConverter::ConvertSynStreamFrame(
    const spdy::SpdySynStreamControlFrame& frame) {
  if (state_ != NO_FRAMES_YET) {
    return EXTRA_SYN_STREAM;
  }
  state_ = RECEIVED_SYN_STREAM;

  spdy::SpdyHeaderBlock block;
  if (!ParseHeaderBlockInBuffer(frame.header_block(), frame.header_block_len(),
                                &block)) {
    return INVALID_HEADER_BLOCK;
  }

  if (!GenerateRequestLine(block, visitor_)) {
    return BAD_REQUEST;
  }

  // Translate the headers to HTTP.
  GenerateHeaders<&HttpStreamVisitorInterface::OnLeadingHeader>(
      block, visitor_);

  // If this is the last (i.e. only) frame on this stream, finish off the HTTP
  // request.
  if (HasControlFlagFinSet(frame)) {
    FinishRequest();
  }

  return SPDY_CONVERTER_SUCCESS;
}

SpdyToHttpConverter::Status SpdyToHttpConverter::ConvertHeadersFrame(
    const spdy::SpdyHeadersControlFrame& frame) {
  if (state_ == RECEIVED_FLAG_FIN) {
    return FRAME_AFTER_FIN;
  } else if (state_ == NO_FRAMES_YET) {
    return FRAME_BEFORE_SYN_STREAM;
  }

  // Parse the headers from the HEADERS frame.  If there have already been any
  // data frames, then we need to save these headers for later and send them as
  // trailing headers.  Otherwise, we can send them immediately.
  if (state_ == RECEIVED_DATA) {
    if (!ParseHeaderBlockInBuffer(frame.header_block(),
                                  frame.header_block_len(),
                                  &trailing_headers_)) {
      return INVALID_HEADER_BLOCK;
    }
  } else {
    DCHECK(state_ == RECEIVED_SYN_STREAM);
    DCHECK(trailing_headers_.empty());
    spdy::SpdyHeaderBlock block;
    if (!ParseHeaderBlockInBuffer(frame.header_block(),
                                  frame.header_block_len(), &block)) {
      return INVALID_HEADER_BLOCK;
    }
    GenerateHeaders<&HttpStreamVisitorInterface::OnLeadingHeader>(
        block, visitor_);
  }

  // If this is the last frame on this stream, finish off the HTTP request.
  if (HasControlFlagFinSet(frame)) {
    FinishRequest();
  }

  return SPDY_CONVERTER_SUCCESS;
}

SpdyToHttpConverter::Status SpdyToHttpConverter::ConvertDataFrame(
    const spdy::SpdyDataFrame& frame) {
  if (state_ == RECEIVED_FLAG_FIN) {
    return FRAME_AFTER_FIN;
  } else if (state_ == NO_FRAMES_YET) {
    return FRAME_BEFORE_SYN_STREAM;
  }

  // If this is the first data frame in the stream, we need to close the HTTP
  // headers section (for streams where there are never any data frames, we
  // close the headers section in FinishRequest instead).  Just before we do,
  // we also need to set Transfer-Encoding: chunked.
  if (state_ != RECEIVED_DATA) {
    DCHECK(state_ == RECEIVED_SYN_STREAM);
    state_ = RECEIVED_DATA;
    visitor_->OnLeadingHeader(kTransferEncoding, kChunked);
    visitor_->OnLeadingHeadersComplete();
  }

  // Translate the SPDY data frame into an HTTP data chunk.
  visitor_->OnDataChunk(base::StringPiece(frame.payload(), frame.length()));

  // If this is the last frame on this stream, finish off the HTTP request.
  if (HasDataFlagFinSet(frame)) {
    FinishRequest();
  }

  return SPDY_CONVERTER_SUCCESS;
}

void SpdyToHttpConverter::FinishRequest() {
  if (state_ == RECEIVED_DATA) {
    // Indicate that there is no more data coming.
    visitor_->OnDataChunksComplete();

    // Append whatever trailing headers we've buffered, if any.
    if (!trailing_headers_.empty()) {
      GenerateHeaders<&HttpStreamVisitorInterface::OnTrailingHeader>(
          trailing_headers_, visitor_);
      trailing_headers_.clear();
      visitor_->OnTrailingHeadersComplete();
    }
  } else {
    DCHECK(state_ == RECEIVED_SYN_STREAM);
    // We only ever add to trailing_headers_ after receiving at least one data
    // frame, so if we haven't received any data frames then trailing_headers_
    // should still be empty.
    DCHECK(trailing_headers_.empty());

    // There were no data frames in this stream, so we haven't closed the
    // normal (non-trailing) headers yet (if there had been any data frames, we
    // would have closed the normal headers in ConvertDataFrame instead).  Do
    // so now.
    visitor_->OnLeadingHeadersComplete();
  }

  // Indicate that this request is finished.
  visitor_->OnComplete();
  state_ = RECEIVED_FLAG_FIN;
}

}  // namespace mod_spdy
