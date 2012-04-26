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
#include "mod_spdy/common/spdy_frame_priority_queue.h"
#include "mod_spdy/common/spdy_frame_queue.h"

namespace mod_spdy {

SpdyStream::SpdyStream(net::SpdyStreamId stream_id,
                       net::SpdyStreamId associated_stream_id,
                       net::SpdyPriority priority,
                       SpdyFramePriorityQueue* output_queue,
                       net::BufferedSpdyFramer* framer)
    : stream_id_(stream_id),
      associated_stream_id_(associated_stream_id),
      priority_(priority),
      output_queue_(output_queue),
      framer_(framer) {
  DCHECK(output_queue_);
  DCHECK(framer_);
}

SpdyStream::~SpdyStream() {}

bool SpdyStream::is_server_push() const {
  // By the SPDY spec, a stream has an even stream ID if and only if it was
  // initiated by the server.
  return stream_id_ % 2 == 0;
}

bool SpdyStream::is_aborted() const {
  return input_queue_.is_aborted();
}

void SpdyStream::AbortSilently() {
  input_queue_.Abort();
}

void SpdyStream::AbortWithRstStream(net::SpdyStatusCodes status) {
  input_queue_.Abort();
  output_queue_->InsertFront(framer_->CreateRstStream(stream_id_, status));
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

void SpdyStream::SendOutputDataFrame(base::StringPiece data, bool flag_fin) {
  const net::SpdyDataFlags flags =
      flag_fin ? net::DATA_FLAG_FIN : net::DATA_FLAG_NONE;
  SendOutputFrame(framer_->CreateDataFrame(
      stream_id_, data.data(), data.size(), flags));
}

void SpdyStream::SendOutputFrame(net::SpdyFrame* frame) {
  output_queue_->Insert(priority_, frame);
}

}  // namespace mod_spdy
