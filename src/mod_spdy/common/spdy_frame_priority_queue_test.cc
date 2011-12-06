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

#include "net/spdy/spdy_framer.h"
#include "net/spdy/spdy_protocol.h"
#include "testing/gtest/include/gtest/gtest.h"

// spdy_protocol.h defines only SPDY_PRIORITY_LOWEST and HIGHEST.  Define the
// others here:
#define SPDY_PRIORITY_MIDHIGH 1
#define SPDY_PRIORITY_MIDLOW 2

namespace {

TEST(SpdyFramePriorityQueueTest, Simple) {
  mod_spdy::SpdyFramePriorityQueue queue;
  spdy::SpdyFrame* frame;
  ASSERT_FALSE(queue.Pop(&frame));

  queue.Insert(SPDY_PRIORITY_LOWEST, spdy::SpdyFramer::CreateGoAway(4));
  queue.Insert(SPDY_PRIORITY_HIGHEST, spdy::SpdyFramer::CreateGoAway(1));
  queue.Insert(SPDY_PRIORITY_LOWEST, spdy::SpdyFramer::CreateGoAway(3));

  ASSERT_TRUE(queue.Pop(&frame));
  ASSERT_EQ(1, static_cast<spdy::SpdyGoAwayControlFrame*>(frame)->
            last_accepted_stream_id());
  delete frame;
  ASSERT_TRUE(queue.Pop(&frame));
  ASSERT_EQ(4, static_cast<spdy::SpdyGoAwayControlFrame*>(frame)->
            last_accepted_stream_id());
  delete frame;

  queue.Insert(SPDY_PRIORITY_MIDLOW, spdy::SpdyFramer::CreateGoAway(2));
  queue.Insert(SPDY_PRIORITY_MIDHIGH, spdy::SpdyFramer::CreateGoAway(6));
  queue.Insert(SPDY_PRIORITY_MIDHIGH, spdy::SpdyFramer::CreateGoAway(5));

  ASSERT_TRUE(queue.Pop(&frame));
  ASSERT_EQ(6, static_cast<spdy::SpdyGoAwayControlFrame*>(frame)->
            last_accepted_stream_id());
  delete frame;
  ASSERT_TRUE(queue.Pop(&frame));
  ASSERT_EQ(5, static_cast<spdy::SpdyGoAwayControlFrame*>(frame)->
            last_accepted_stream_id());
  delete frame;
  ASSERT_TRUE(queue.Pop(&frame));
  ASSERT_EQ(2, static_cast<spdy::SpdyGoAwayControlFrame*>(frame)->
            last_accepted_stream_id());
  delete frame;
  ASSERT_TRUE(queue.Pop(&frame));
  ASSERT_EQ(3, static_cast<spdy::SpdyGoAwayControlFrame*>(frame)->
            last_accepted_stream_id());
  delete frame;
  ASSERT_FALSE(queue.Pop(&frame));
}

}  // namespace
