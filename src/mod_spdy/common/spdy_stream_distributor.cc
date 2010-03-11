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

#include "mod_spdy/common/spdy_stream_distributor.h"

#include "base/stl_util-inl.h"

namespace mod_spdy {

SpdyFramerVisitorFactoryInterface::SpdyFramerVisitorFactoryInterface() {}
SpdyFramerVisitorFactoryInterface::~SpdyFramerVisitorFactoryInterface() {}

SpdyStreamDistributor::SpdyStreamDistributor(
    spdy::SpdyFramer *framer,
    SpdyFramerVisitorFactoryInterface *factory)
    : framer_(framer), factory_(factory), error_(false) {
}

SpdyStreamDistributor::~SpdyStreamDistributor() {
  STLDeleteContainerPairSecondPointers(map_.begin(), map_.end());
}

void SpdyStreamDistributor::OnError(spdy::SpdyFramer *framer) {
  DCHECK(false);
  error_ = true;
}

void SpdyStreamDistributor::OnControl(const spdy::SpdyControlFrame *frame) {
  if (HasError()) {
    return;
  }

  if (IsStreamControlFrame(frame)) {
    OnStreamControl(frame);
  } else {
    OnSessionControl(frame);
  }
}

bool SpdyStreamDistributor::IsStreamControlFrame(
    const spdy::SpdyControlFrame *frame) const {
  switch (frame->type()) {
    case spdy::SYN_STREAM:
    case spdy::SYN_REPLY:
    case spdy::RST_STREAM:
    case spdy::HEADERS:
      return true;

    case spdy::HELLO:
    case spdy::NOOP:
    case spdy::PING:
    case spdy::GOAWAY:
      return false;

    default:
      LOG(DFATAL) << "Unknown frame type " << frame->type();
      return false;
  }
}

void SpdyStreamDistributor::OnStreamControl(
    const spdy::SpdyControlFrame *frame) {
  const spdy::SpdyStreamId stream_id = frame->stream_id();
  const bool have_stream_visitor = map_.count(stream_id) == 1;
  const bool is_syn_stream = frame->type() == spdy::SYN_STREAM;
  if (is_syn_stream == have_stream_visitor) {
    LOG(DFATAL) << "Mismatch of frame type and visitor state ("
                << is_syn_stream << ") for stream id " << stream_id;
    error_ = true;
    return;
  }

  spdy::SpdyFramerVisitorInterface *visitor = GetFramerForStreamId(stream_id);
  visitor->OnControl(frame);
  if (frame->type() == spdy::RST_STREAM ||
      frame->flags() & spdy::CONTROL_FLAG_FIN) {
    map_.erase(stream_id);
    delete visitor;
  }
}

void SpdyStreamDistributor::OnSessionControl(
    const spdy::SpdyControlFrame *frame) {
  switch (frame->type()) {
    case spdy::NOOP:
      // Nothing to do.
      break;

    // TODO: handle HELLO, PING, and GOAWAY

    default:
      LOG(DFATAL) << "Unexpected frame type: " << frame->type();
      error_ = true;
      break;
  }
}

void SpdyStreamDistributor::OnStreamFrameData(spdy::SpdyStreamId stream_id,
                                              const char *data,
                                              size_t len) {
  if (HasError()) {
    return;
  }
  if (map_.find(stream_id) == map_.end()) {
    LOG(DFATAL) << "Unable to find handler for stream id " << stream_id;
    error_ = true;
    return;
  }

  spdy::SpdyFramerVisitorInterface *visitor = GetFramerForStreamId(stream_id);
  visitor->OnStreamFrameData(stream_id, data, len);
  if (len == 0) {
    // This is the special callback indicating that the stream is
    // complete, so we should now free the associated visitor.
    map_.erase(stream_id);
    delete visitor;
  }
}

spdy::SpdyFramerVisitorInterface *SpdyStreamDistributor::GetFramerForStreamId(
    spdy::SpdyStreamId id) {
  StreamIdToVisitorMap::const_iterator it = map_.find(id);
  if (it == map_.end()) {
    map_[id] = factory_->Create(id);
  }
  return map_[id];
}

}  // namespace mod_spdy
