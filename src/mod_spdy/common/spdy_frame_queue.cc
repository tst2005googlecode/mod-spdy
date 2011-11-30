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

#include "mod_spdy/common/spdy_frame_queue.h"

#include "base/stl_util-inl.h"
#include "base/synchronization/lock.h"

namespace mod_spdy {

SpdyFrameQueue::SpdyFrameQueue() {}

SpdyFrameQueue::~SpdyFrameQueue() {
  STLDeleteContainerPointers(queue_.begin(), queue_.end());
}

void SpdyFrameQueue::Insert(spdy::SpdyFrame* frame) {
  base::AutoLock autolock(lock_);
  queue_.push_front(frame);
}

bool SpdyFrameQueue::Pop(spdy::SpdyFrame** frame) {
  base::AutoLock autolock(lock_);
  if (queue_.empty()) {
    return false;
  }
  *frame = queue_.back();
  queue_.pop_back();
  return true;
}

}  // namespace mod_spdy
