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

#include "net/spdy/spdy_framer.h"
#include "net/spdy/spdy_protocol.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

const int kSpdyVersion = 2;

net::SpdyStreamId GetPingId(net::SpdyFrame* frame) {
  if (!frame->is_control_frame() ||
      static_cast<net::SpdyControlFrame*>(frame)->type() != net::PING) {
    ADD_FAILURE() << "Frame is not a PING frame.";
    return 0;
  }
  return static_cast<net::SpdyPingControlFrame*>(frame)->unique_id();
}

void ExpectPop(net::SpdyStreamId expected, mod_spdy::SpdyFrameQueue* queue) {
  net::SpdyFrame* raw_frame = NULL;
  const bool success = queue->Pop(false, &raw_frame);
  scoped_ptr<net::SpdyFrame> scoped_frame(raw_frame);
  EXPECT_TRUE(success);
  ASSERT_TRUE(scoped_frame != NULL);
  ASSERT_EQ(expected, GetPingId(scoped_frame.get()));
}

void ExpectEmpty(mod_spdy::SpdyFrameQueue* queue) {
  net::SpdyFrame* frame = NULL;
  EXPECT_FALSE(queue->Pop(false, &frame));
  EXPECT_TRUE(frame == NULL);
}

TEST(SpdyFrameQueueTest, Simple) {
  net::SpdyFramer framer(kSpdyVersion);
  mod_spdy::SpdyFrameQueue queue;
  ExpectEmpty(&queue);

  queue.Insert(framer.CreatePingFrame(4));
  queue.Insert(framer.CreatePingFrame(1));
  queue.Insert(framer.CreatePingFrame(3));

  ExpectPop(4, &queue);
  ExpectPop(1, &queue);

  queue.Insert(framer.CreatePingFrame(2));
  queue.Insert(framer.CreatePingFrame(5));

  ExpectPop(3, &queue);
  ExpectPop(2, &queue);
  ExpectPop(5, &queue);
  ExpectEmpty(&queue);
}

TEST(SpdyFrameQueueTest, AbortEmptiesQueue) {
  net::SpdyFramer framer(kSpdyVersion);
  mod_spdy::SpdyFrameQueue queue;
  ASSERT_FALSE(queue.is_aborted());
  ExpectEmpty(&queue);

  queue.Insert(framer.CreatePingFrame(4));
  queue.Insert(framer.CreatePingFrame(1));
  queue.Insert(framer.CreatePingFrame(3));

  ExpectPop(4, &queue);

  queue.Abort();

  ExpectEmpty(&queue);
  ASSERT_TRUE(queue.is_aborted());
}

// TODO(mdsteele): Add tests for threaded behavior and blocking Pop() calls.

}  // namespace
