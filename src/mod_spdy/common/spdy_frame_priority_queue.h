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

#ifndef MOD_SPDY_COMMON_SPDY_FRAME_PRIORITY_QUEUE_H_
#define MOD_SPDY_COMMON_SPDY_FRAME_PRIORITY_QUEUE_H_

#include <list>

#include "base/basictypes.h"
#include "base/synchronization/condition_variable.h"
#include "base/synchronization/lock.h"
#include "net/spdy/spdy_protocol.h"

namespace base { class TimeDelta; }

namespace mod_spdy {

// A priority queue of SPDY frames, intended for multiplexing output frames
// from multiple SPDY stream threads back to the SPDY connection thread and
// allowing frames from high-priority streams to cut in front of lower-priority
// streams.  This class is thread-safe -- its methods may be called
// concurrently by multiple threads.
class SpdyFramePriorityQueue {
 public:
  // Create an initially-empty queue.
  SpdyFramePriorityQueue();
  ~SpdyFramePriorityQueue();

  // Insert a frame into the queue at the specified priority.  The queue takes
  // ownership of the frame, and will delete it if the queue is deleted before
  // the frame is removed from the queue by the Pop method.
  void Insert(spdy::SpdyPriority priority, spdy::SpdyFrame* frame);

  // Insert a frame at the *front* of the queue; it will be popped out before
  // any other frame currently in the queue, regardless of priority.  The queue
  // takes ownership of the frame, and will delete it if the queue is deleted
  // before the frame is removed from the queue by the Pop method.
  void InsertFront(spdy::SpdyFrame* frame);

  // Remove and provide a frame from the queue and return true, or return false
  // if the queue is empty.  The caller gains ownership of the provided frame
  // object.  This method will try to yield higher-priority frames before
  // lower-priority ones (even if they were inserted later), but guarantees to
  // return same-priority frames in the same order they were inserted (FIFO).
  // In particular, this means that a sequence of frames from the same SPDY
  // stream will stay in order (assuming they were all inserted with the same
  // priority -- that of the stream).
  bool Pop(spdy::SpdyFrame** frame);

  // Like Pop(), but if the queue is empty this method will block for up to
  // max_time before returning false.
  bool BlockingPop(const base::TimeDelta& max_time, spdy::SpdyFrame** frame);

 private:
  base::Lock lock_;
  base::ConditionVariable condvar_;
  std::list<spdy::SpdyFrame*> p0_queue_;
  std::list<spdy::SpdyFrame*> p1_queue_;
  std::list<spdy::SpdyFrame*> p2_queue_;
  std::list<spdy::SpdyFrame*> p3_queue_;

  DISALLOW_COPY_AND_ASSIGN(SpdyFramePriorityQueue);
};

}  // namespace mod_spdy

#endif  // MOD_SPDY_COMMON_SPDY_FRAME_PRIORITY_QUEUE_H_
