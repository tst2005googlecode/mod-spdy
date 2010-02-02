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

#include "mod_spdy/common/flip_frame_pump.h"

#include "base/logging.h"
#include "mod_spdy/common/input_stream_interface.h"
#include "net/flip/flip_framer.h"

namespace {
const size_t kBufSize = 4096;
}  // namespace

namespace mod_spdy {

FlipFramePump::FlipFramePump(InputStreamInterface *input,
                             flip::FlipFramerVisitorInterface *visitor)
    : input_(input),
      visitor_(visitor),
      framer_(new flip::FlipFramer()),
      buf_(new char[kBufSize]),
      frame_bytes_consumed_(0) {
  framer_->set_visitor(visitor_);
}

FlipFramePump::~FlipFramePump() {
}

bool FlipFramePump::PumpOneFrame() {
  // Loop until we reach a frame boundary.
  do {
    if (framer_->HasError()) {
      return false;
    }
    if (!PumpMoreBytes()) {
      return false;
    }
  } while (!framer_->MessageFullyRead());

  return framer_->MessageFullyRead();
}

bool FlipFramePump::HasError() const {
  return framer_->HasError();
}

bool FlipFramePump::PumpMoreBytes() {
  // TODO: push the logic to compute number of bytes to read into
  // FlipFramer.
  size_t common_header_bytes_remaining;
  switch (framer_->state()) {
    case flip::FlipFramer::FLIP_RESET:
    case flip::FlipFramer::FLIP_AUTO_RESET:
      frame_bytes_consumed_ = 0;
      return PumpAtMost(flip::FlipFrame::size());

    case flip::FlipFramer::FLIP_READING_COMMON_HEADER:
      common_header_bytes_remaining =
          flip::FlipFrame::size() - frame_bytes_consumed_;
      CHECK(common_header_bytes_remaining > 0);
      return PumpAtMost(common_header_bytes_remaining);

    case flip::FlipFramer::FLIP_INTERPRET_CONTROL_FRAME_COMMON_HEADER:
    case flip::FlipFramer::FLIP_CONTROL_FRAME_PAYLOAD:
    case flip::FlipFramer::FLIP_FORWARD_STREAM_FRAME:
    case flip::FlipFramer::FLIP_IGNORE_REMAINING_PAYLOAD:
      CHECK(framer_->remaining_payload() > 0);
      return PumpAtMost(framer_->remaining_payload());

    // These should never happen.
    case flip::FlipFramer::FLIP_DONE:
    case flip::FlipFramer::FLIP_ERROR:
      CHECK(false);
      return false;

    default:
      CHECK(false);
      return false;
  }
}

bool FlipFramePump::PumpAtMost(size_t num_bytes) {
  num_bytes = std::min(num_bytes, kBufSize);
  CHECK(num_bytes > 0);
  const size_t bytes_available = input_->Read(buf_.get(), num_bytes);
  if (bytes_available == 0) {
    // Nothing to read. Abort early.
    return false;
  }
  frame_bytes_consumed_ += bytes_available;

  size_t actual_bytes_consumed =
      framer_->ProcessInput(buf_.get(), bytes_available);

  // For now we expect the FlipFramer to consume all available bytes.
  // TODO: find out if it's possible for the FlipFramer to refuse to
  // consume some bytes.
  CHECK(actual_bytes_consumed == bytes_available);

  // Indicate whether we consumed as many bytes as expected.
  return actual_bytes_consumed == num_bytes;
}

}  // namespace mod_spdy
