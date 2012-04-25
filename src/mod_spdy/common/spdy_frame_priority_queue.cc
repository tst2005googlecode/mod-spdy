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

#include "base/stl_util.h"
#include "base/synchronization/condition_variable.h"
#include "base/synchronization/lock.h"
#include "base/time.h"

namespace {

bool TryPopFrom(std::list<net::SpdyFrame*>* queue, net::SpdyFrame** frame) {
  DCHECK(frame);
  if (queue->empty()) {
    return false;
  }
  *frame = queue->front();
  queue->pop_front();
  return true;
}

}  // namespace

namespace mod_spdy {

SpdyFramePriorityQueue::SpdyFramePriorityQueue()
    : condvar_(&lock_) {}

SpdyFramePriorityQueue::~SpdyFramePriorityQueue() {
  STLDeleteContainerPointers(p0_queue_.begin(), p0_queue_.end());
  STLDeleteContainerPointers(p1_queue_.begin(), p1_queue_.end());
  STLDeleteContainerPointers(p2_queue_.begin(), p2_queue_.end());
  STLDeleteContainerPointers(p3_queue_.begin(), p3_queue_.end());
}

bool SpdyFramePriorityQueue::IsEmpty() const {
  base::AutoLock autolock(lock_);
  return (p0_queue_.empty() && p1_queue_.empty() &&
          p2_queue_.empty() && p3_queue_.empty());
}

void SpdyFramePriorityQueue::Insert(net::SpdyPriority priority,
                                    net::SpdyFrame* frame) {
  base::AutoLock autolock(lock_);
  DCHECK(frame);
  switch (priority) {
    case 0:
      p0_queue_.push_back(frame);
      break;
    case 1:
      p1_queue_.push_back(frame);
      break;
    case 2:
      p2_queue_.push_back(frame);
      break;
    case 3:
      p3_queue_.push_back(frame);
      break;
    default:
      LOG(DFATAL) << "Invalid priority value: " << priority;
      p3_queue_.push_back(frame);
  }
  condvar_.Signal();
}

void SpdyFramePriorityQueue::InsertFront(net::SpdyFrame* frame) {
  base::AutoLock autolock(lock_);
  DCHECK(frame);
  // To ensure that this frame is at the very front of the queue, we push it at
  // the front of the highest-priority internal queue.
  p0_queue_.push_front(frame);
  condvar_.Signal();
}

bool SpdyFramePriorityQueue::Pop(net::SpdyFrame** frame) {
  base::AutoLock autolock(lock_);
  DCHECK(frame);
  return (TryPopFrom(&p0_queue_, frame) ||
          TryPopFrom(&p1_queue_, frame) ||
          TryPopFrom(&p2_queue_, frame) ||
          TryPopFrom(&p3_queue_, frame));
}

bool SpdyFramePriorityQueue::BlockingPop(const base::TimeDelta& max_time,
                                         net::SpdyFrame** frame) {
  base::AutoLock autolock(lock_);
  DCHECK(frame);

  const base::TimeDelta zero = base::TimeDelta();
  base::TimeDelta time_remaining = max_time;
  while (time_remaining > zero &&
         p0_queue_.empty() && p1_queue_.empty() &&
         p2_queue_.empty() && p3_queue_.empty()) {
    // TODO(mdsteele): It appears from looking at the Chromium source code that
    // HighResNow() is "expensive" on Windows (how expensive, I am not sure);
    // however, the other options for getting a "now" time either don't
    // guarantee monotonicity (so time might go backwards) or might be too
    // low-resolution for our purposes, so I think we'd better stick with this
    // for now.  But is there a better way to do what we're doing here?
    const base::TimeTicks start = base::TimeTicks::HighResNow();
    condvar_.TimedWait(time_remaining);
    time_remaining -= base::TimeTicks::HighResNow() - start;
  }

  return (TryPopFrom(&p0_queue_, frame) ||
          TryPopFrom(&p1_queue_, frame) ||
          TryPopFrom(&p2_queue_, frame) ||
          TryPopFrom(&p3_queue_, frame));
}

}  // namespace mod_spdy
