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

#include "mod_spdy/common/spdy_frame_priority_queue.h"
#include "mod_spdy/common/spdy_frame_queue.h"

namespace mod_spdy {

SpdyStream::SpdyStream(net::SpdyStreamId stream_id,
                       net::SpdyStreamId associated_stream_id,
                       net::SpdyPriority priority,
                       SpdyFramePriorityQueue* output_queue)
    : stream_id_(stream_id),
      associated_stream_id_(associated_stream_id),
      priority_(priority),
      output_queue_(output_queue) {}

SpdyStream::~SpdyStream() {}

bool SpdyStream::is_server_push() const {
  // By the SPDY spec, a stream has an even stream ID if and only if it was
  // initiated by the server.
  return stream_id_ % 2 == 0;
}

bool SpdyStream::is_aborted() const {
  return input_queue_.is_aborted();
}

void SpdyStream::Abort() {
  input_queue_.Abort();
}

void SpdyStream::PostInputFrame(net::SpdyFrame* frame) {
  input_queue_.Insert(frame);
}

bool SpdyStream::GetInputFrame(bool block, net::SpdyFrame** frame) {
  return input_queue_.Pop(block, frame);
}

void SpdyStream::SendOutputFrame(net::SpdyFrame* frame) {
  output_queue_->Insert(priority_, frame);
}

}  // namespace mod_spdy
