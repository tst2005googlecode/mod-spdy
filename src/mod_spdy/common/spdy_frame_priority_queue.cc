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

#include "mod_spdy/common/spdy_frame_priority_queue.h"

#include <list>

#include "base/stl_util-inl.h"
#include "base/synchronization/lock.h"

namespace {

bool TryPopFrom(std::list<spdy::SpdyFrame*>* queue, spdy::SpdyFrame** frame) {
  if (queue->empty()) {
    return false;
  }
  *frame = queue->back();
  queue->pop_back();
  return true;
}

}  // namespace

namespace mod_spdy {

SpdyFramePriorityQueue::SpdyFramePriorityQueue() {}

SpdyFramePriorityQueue::~SpdyFramePriorityQueue() {
  STLDeleteContainerPointers(p0_queue_.begin(), p0_queue_.end());
  STLDeleteContainerPointers(p1_queue_.begin(), p1_queue_.end());
  STLDeleteContainerPointers(p2_queue_.begin(), p2_queue_.end());
  STLDeleteContainerPointers(p3_queue_.begin(), p3_queue_.end());
}

void SpdyFramePriorityQueue::Insert(spdy::SpdyPriority priority,
                                    spdy::SpdyFrame* frame) {
  base::AutoLock autolock(lock_);
  switch (priority) {
    case 0:
      p0_queue_.push_front(frame);
      break;
    case 1:
      p1_queue_.push_front(frame);
      break;
    case 2:
      p2_queue_.push_front(frame);
      break;
    case 3:
      p3_queue_.push_front(frame);
      break;
    default:
      LOG(DFATAL) << "Invalid priority value: " << priority;
      p3_queue_.push_front(frame);
  }
}

bool SpdyFramePriorityQueue::Pop(spdy::SpdyFrame** frame) {
  base::AutoLock autolock(lock_);
  return (TryPopFrom(&p0_queue_, frame) ||
          TryPopFrom(&p1_queue_, frame) ||
          TryPopFrom(&p2_queue_, frame) ||
          TryPopFrom(&p3_queue_, frame));
}

}  // namespace mod_spdy
