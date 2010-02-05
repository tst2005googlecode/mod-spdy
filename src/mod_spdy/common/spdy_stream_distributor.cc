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

FlipFramerVisitorFactoryInterface::FlipFramerVisitorFactoryInterface() {}
FlipFramerVisitorFactoryInterface::~FlipFramerVisitorFactoryInterface() {}

SpdyStreamDistributor::SpdyStreamDistributor(
    flip::FlipFramer *framer,
    FlipFramerVisitorFactoryInterface *factory)
    : framer_(framer), factory_(factory) {
}

SpdyStreamDistributor::~SpdyStreamDistributor() {
  STLDeleteContainerPairSecondPointers(map_.begin(), map_.end());
}

void SpdyStreamDistributor::OnError(flip::FlipFramer *framer) {
  CHECK(false);
}

void SpdyStreamDistributor::OnControl(const flip::FlipControlFrame *frame) {
  const flip::FlipStreamId stream_id = frame->stream_id();
  const bool have_stream_visitor = map_.count(stream_id) == 1;
  const bool is_syn_stream = frame->type() == flip::SYN_STREAM;
  CHECK(is_syn_stream != have_stream_visitor);

  flip::FlipFramerVisitorInterface *visitor = GetFramerForStreamId(stream_id);
  visitor->OnControl(frame);
  if (frame->flags() & flip::CONTROL_FLAG_FIN) {
    map_.erase(stream_id);
    delete visitor;
  }
}

void SpdyStreamDistributor::OnStreamFrameData(flip::FlipStreamId stream_id,
                                              const char *data,
                                              size_t len) {
  CHECK(map_.find(stream_id) != map_.end());

  flip::FlipFramerVisitorInterface *visitor = GetFramerForStreamId(stream_id);
  visitor->OnStreamFrameData(stream_id, data, len);
  if (len == 0) {
    map_.erase(stream_id);
    delete visitor;
  }
}

flip::FlipFramerVisitorInterface *SpdyStreamDistributor::GetFramerForStreamId(
    flip::FlipStreamId id) {
  StreamIdToVisitorMap::const_iterator it = map_.find(id);
  if (it == map_.end()) {
    map_[id] = factory_->Create(id);
  }
  return map_[id];
}

}  // namespace mod_spdy
