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

#ifndef MOD_SPDY_COMMON_SPDY_FRAME_QUEUE_H_
#define MOD_SPDY_COMMON_SPDY_FRAME_QUEUE_H_

#include <list>

#include "base/basictypes.h"
#include "base/synchronization/lock.h"
#include "net/spdy/spdy_protocol.h"

namespace mod_spdy {

// A simple FIFO queue of SPDY frames, intended for sending input frames from
// the SPDY connection thread to a SPDY stream thread.  This class is
// thread-safe -- the Insert() and Pop() methods may be called concurrently by
// multiple threads.
class SpdyFrameQueue {
 public:
  // Create an initially-empty queue.
  SpdyFrameQueue();
  ~SpdyFrameQueue();

  // Insert a frame into the queue.  The queue takes ownership of the frame,
  // and will delete it if the queue is deleted before the frame is removed
  // from the queue by the Pop method.
  void Insert(spdy::SpdyFrame* frame);

  // Remove and provide a frame from the queue and return true, or return false
  // if the queue is empty.  The caller gains ownership of the provided frame
  // object.
  bool Pop(spdy::SpdyFrame** frame);

 private:
  // This is a pretty naive implementation of a thread-safe queue, but it's
  // good enough for our purposes.  We could use an apr_queue_t instead of
  // rolling our own class, but it lacks the ownership semantics that we want.
  base::Lock lock_;
  std::list<spdy::SpdyFrame*> queue_;

  DISALLOW_COPY_AND_ASSIGN(SpdyFrameQueue);
};

}  // namespace mod_spdy

#endif  // MOD_SPDY_COMMON_SPDY_FRAME_QUEUE_H_
