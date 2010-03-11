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

#include "mod_spdy/common/spdy_frame_pump.h"

#include "base/logging.h"
#include "mod_spdy/common/input_stream_interface.h"
#include "net/spdy/spdy_framer.h"

namespace {
const size_t kBufSize = 4096;
}  // namespace

namespace mod_spdy {

SpdyFramePump::SpdyFramePump(InputStreamInterface *input,
                             spdy::SpdyFramer *framer)
    : input_(input),
      framer_(framer),
      buf_(new char[kBufSize]),
      frame_bytes_consumed_(0),
      error_(false) {
}

SpdyFramePump::~SpdyFramePump() {
}

bool SpdyFramePump::PumpOneFrame() {
  // Loop until we reach a frame boundary.
  do {
    if (HasError()) {
      return false;
    }
    if (!PumpMoreBytes()) {
      return false;
    }
  } while (!framer_->MessageFullyRead());

  return framer_->MessageFullyRead();
}

bool SpdyFramePump::HasError() const {
  return error_ || framer_->HasError();
}

bool SpdyFramePump::PumpMoreBytes() {
  // TODO: push the logic to compute number of bytes to read into
  // SpdyFramer.
  size_t common_header_bytes_remaining;
  switch (framer_->state()) {
    case spdy::SpdyFramer::SPDY_RESET:
    case spdy::SpdyFramer::SPDY_AUTO_RESET:
      frame_bytes_consumed_ = 0;
      return PumpAtMost(spdy::SpdyFrame::size());

    case spdy::SpdyFramer::SPDY_READING_COMMON_HEADER:
      common_header_bytes_remaining =
          spdy::SpdyFrame::size() - frame_bytes_consumed_;
      if (common_header_bytes_remaining <= 0) {
        LOG(DFATAL) << "Unable to make progress while reading common header.";
        error_ = true;
        return false;
      }
      return PumpAtMost(common_header_bytes_remaining);

    case spdy::SpdyFramer::SPDY_INTERPRET_CONTROL_FRAME_COMMON_HEADER:
    case spdy::SpdyFramer::SPDY_CONTROL_FRAME_PAYLOAD:
    case spdy::SpdyFramer::SPDY_FORWARD_STREAM_FRAME:
    case spdy::SpdyFramer::SPDY_IGNORE_REMAINING_PAYLOAD:
      if (framer_->remaining_payload() <= 0) {
        LOG(DFATAL) << "Unable to make progress while reading payload.";
        error_ = true;
        return false;
      }
      return PumpAtMost(framer_->remaining_payload());

    // These should never happen.
    case spdy::SpdyFramer::SPDY_DONE:
    case spdy::SpdyFramer::SPDY_ERROR:
      LOG(DFATAL) << "Encountered unexpected framer state " << framer_->state();
      error_ = true;
      return false;

    default:
      LOG(DFATAL) << "Encountered unknown framer state " << framer_->state();
      error_ = true;
      return false;
  }
}

bool SpdyFramePump::PumpAtMost(size_t num_bytes) {
  num_bytes = std::min(num_bytes, kBufSize);
  if (num_bytes <= 0) {
    DCHECK(false);
    error_ = true;
    return false;
  }
  const size_t bytes_available = input_->Read(buf_.get(), num_bytes);
  if (bytes_available == 0) {
    // Nothing to read. Abort early.
    return false;
  }
  frame_bytes_consumed_ += bytes_available;

  size_t actual_bytes_consumed =
      framer_->ProcessInput(buf_.get(), bytes_available);

  // For now we expect the SpdyFramer to consume all available bytes.
  // TODO: find out if it's possible for the SpdyFramer to refuse to
  // consume some bytes.
  if (actual_bytes_consumed != bytes_available) {
    LOG(DFATAL) << "SpdyFramer consumed " << actual_bytes_consumed
                << " bytes of " << bytes_available << " total bytes.";
    error_ = true;
    return false;
  }

  // Indicate whether we consumed as many bytes as expected.
  return actual_bytes_consumed == num_bytes;
}

}  // namespace mod_spdy
