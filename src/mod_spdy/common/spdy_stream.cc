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

#include "mod_spdy/common/spdy_stream.h"


#include "base/logging.h"
#include "base/synchronization/condition_variable.h"
#include "base/synchronization/lock.h"
#include "mod_spdy/common/spdy_frame_priority_queue.h"
#include "mod_spdy/common/spdy_frame_queue.h"

namespace mod_spdy {

SpdyStream::SpdyStream(net::SpdyStreamId stream_id,
                       net::SpdyStreamId associated_stream_id,
                       net::SpdyPriority priority,
                       int32 initial_window_size,
                       SpdyFramePriorityQueue* output_queue,
                       net::BufferedSpdyFramer* framer)
    : stream_id_(stream_id),
      associated_stream_id_(associated_stream_id),
      priority_(priority),
      output_queue_(output_queue),
      framer_(framer),
      condvar_(&lock_),
      window_size_(initial_window_size),
      aborted_(false) {
  DCHECK(output_queue_);
  DCHECK(framer_);
  DCHECK_GT(window_size_, 0);
}

SpdyStream::~SpdyStream() {}

bool SpdyStream::is_server_push() const {
  // By the SPDY spec, a stream has an even stream ID if and only if it was
  // initiated by the server.
  return stream_id_ % 2 == 0;
}

bool SpdyStream::is_aborted() const {
  base::AutoLock autolock(lock_);
  return aborted_;
}

void SpdyStream::AbortSilently() {
  base::AutoLock autolock(lock_);
  InternalAbortSilently();
}

void SpdyStream::AbortWithRstStream(net::SpdyStatusCodes status) {
  base::AutoLock autolock(lock_);
  InternalAbortWithRstStream(status);
}

int32 SpdyStream::current_window_size() const {
  base::AutoLock autolock(lock_);
  return window_size_;
}

void SpdyStream::AdjustWindowSize(int32 delta) {
  base::AutoLock autolock(lock_);

  // Flow control only exists for SPDY v3 and up.
  DCHECK_GE(spdy_version(), 3);

  if (aborted_) {
    return;
  }

  // Check for overflow; if it happens, abort the stream (which will wake up
  // any blocked threads).  Note that although delta is usually positive, it
  // can also be negative, so we check for both overflow and underflow.
  const int64 new_size =
      static_cast<int64>(window_size_) + static_cast<int64>(delta);
  if (new_size > static_cast<int64>(net::kSpdyStreamMaximumWindowSize) ||
      new_size < -static_cast<int64>(net::kSpdyStreamMaximumWindowSize)) {
    InternalAbortWithRstStream(net::FLOW_CONTROL_ERROR);
    return;
  }

  // Update the window size.
  const int32 old_size = window_size_;
  window_size_ = static_cast<int32>(new_size);

  // If the window size is newly positive, wake up any blocked threads.
  if (old_size <= 0 && window_size_ > 0) {
    condvar_.Broadcast();
  }
}

void SpdyStream::PostInputFrame(net::SpdyFrame* frame) {
  input_queue_.Insert(frame);
}

bool SpdyStream::GetInputFrame(bool block, net::SpdyFrame** frame) {
  return input_queue_.Pop(block, frame);
}

void SpdyStream::SendOutputSynStream(const net::SpdyHeaderBlock& headers,
                                     bool flag_fin) {
  DCHECK(is_server_push());
  base::AutoLock autolock(lock_);
  if (aborted_) {
    return;
  }

  const net::SpdyControlFlags flags = static_cast<net::SpdyControlFlags>(
      (flag_fin ? net::CONTROL_FLAG_FIN : net::CONTROL_FLAG_NONE) |
      net::CONTROL_FLAG_UNIDIRECTIONAL);
  // Don't compress the headers in the frame here; it will be compressed later
  // by the master connection (which maintains the shared header compression
  // state for all streams).
  SendOutputFrame(framer_->CreateSynStream(
      stream_id_, associated_stream_id_, priority_,
      0,  // 0 = no credential slot
      flags,
      false,  // false = uncompressed
      &headers));
}

void SpdyStream::SendOutputSynReply(const net::SpdyHeaderBlock& headers,
                                    bool flag_fin) {
  DCHECK(!is_server_push());
  base::AutoLock autolock(lock_);
  if (aborted_) {
    return;
  }

  const net::SpdyControlFlags flags =
      flag_fin ? net::CONTROL_FLAG_FIN : net::CONTROL_FLAG_NONE;
  // Don't compress the headers in the frame here; it will be compressed later
  // by the master connection (which maintains the shared header compression
  // state for all streams).
  SendOutputFrame(framer_->CreateSynReply(
      stream_id_, flags,
      false,  // false = uncompressed
      &headers));
}

void SpdyStream::SendOutputHeaders(const net::SpdyHeaderBlock& headers,
                                   bool flag_fin) {
  base::AutoLock autolock(lock_);
  if (aborted_) {
    return;
  }

  const net::SpdyControlFlags flags =
      flag_fin ? net::CONTROL_FLAG_FIN : net::CONTROL_FLAG_NONE;
  // Don't compress the headers in the frame here; it will be compressed later
  // by the master connection (which maintains the shared header compression
  // state for all streams).
  SendOutputFrame(framer_->CreateHeaders(
      stream_id_, flags,
      false,  // false = uncompressed
      &headers));
}

void SpdyStream::SendOutputWindowUpdate(size_t delta) {
  base::AutoLock autolock(lock_);

  // Flow control only exists for SPDY v3 and up.
  DCHECK_GE(spdy_version(), 3);
  // The SPDY spec forbids sending WINDOW_UPDATE frames with a non-positive
  // delta-window-size (SPDY draft 3 section 2.6.8).
  DCHECK_GT(delta, 0u);
  // Make sure there won't be any overflow shenanigans.
  COMPILE_ASSERT(sizeof(size_t) >= sizeof(net::kSpdyStreamMaximumWindowSize),
                 size_t_is_at_least_32_bits);
  DCHECK_LE(delta, static_cast<size_t>(net::kSpdyStreamMaximumWindowSize));

  if (aborted_) {
    return;
  }

  SendOutputFrame(framer_->CreateWindowUpdate(
      stream_id_, static_cast<uint32>(delta)));
}

void SpdyStream::SendOutputDataFrame(base::StringPiece data, bool flag_fin) {
  base::AutoLock autolock(lock_);
  if (aborted_) {
    return;
  }

  // Flow control only exists for SPDY v3 and up; for SPDY v2, we can just send
  // the data without regard to the window size.  Even with flow control, we
  // can of course send empty DATA frames at will.
  if (spdy_version() < 3 || data.empty()) {
    // Suppress empty DATA frames (unless we're setting FLAG_FIN).
    if (!data.empty() || flag_fin) {
      const net::SpdyDataFlags flags =
          flag_fin ? net::DATA_FLAG_FIN : net::DATA_FLAG_NONE;
      SendOutputFrame(framer_->CreateDataFrame(
          stream_id_, data.data(), data.size(), flags));
    }
    return;
  }

  while (!data.empty()) {
    // If the current window size is non-positive, we must wait to send data
    // until the client increases it (or we abort).  Note that the window size
    // can be negative if the client decreased the maximum window size (with a
    // SETTINGS frame) after we already sent data (SPDY draft 3 section 2.6.8).
    while (!aborted_ && window_size_ <= 0) {
      condvar_.Wait();
    }
    if (aborted_) {
      return;
    }

    // If the current window size is less than the amount of data we'd like to
    // send, send a smaller data frame with the first part of the data, and
    // then we'll sleep until the window size is increased before sending the
    // rest.
    DCHECK_GT(window_size_, 0);
    const size_t length = std::min(
        data.size(), static_cast<size_t>(window_size_));
    const net::SpdyDataFlags flags =
        flag_fin && length == data.size() ?
        net::DATA_FLAG_FIN : net::DATA_FLAG_NONE;
    SendOutputFrame(framer_->CreateDataFrame(
        stream_id_, data.data(), length, flags));
    data = data.substr(length);
    window_size_ -= length;
    DCHECK_GE(window_size_, 0);
  }
}

void SpdyStream::SendOutputFrame(net::SpdyFrame* frame) {
  lock_.AssertAcquired();
  DCHECK(!aborted_);
  output_queue_->Insert(priority_, frame);
}

void SpdyStream::InternalAbortSilently() {
  lock_.AssertAcquired();
  input_queue_.Abort();
  aborted_ = true;
  condvar_.Broadcast();
}

void SpdyStream::InternalAbortWithRstStream(net::SpdyStatusCodes status) {
  lock_.AssertAcquired();
  output_queue_->InsertFront(framer_->CreateRstStream(stream_id_, status));
  // InternalAbortSilently will set aborted_ to true, which will prevent the
  // stream thread from sending any more frames on this stream after the
  // RST_STREAM.
  InternalAbortSilently();
}

}  // namespace mod_spdy
