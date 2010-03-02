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

#include "base/scoped_ptr.h"
#include "mod_spdy/common/spdy_stream_distributor.h"
#include "net/spdy/spdy_framer.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

using testing::Eq;
using testing::InSequence;
using testing::IsNull;
using testing::Return;
using testing::StrEq;

const char *kMethod = "GET";
const char *kUrl = "http://www.example.com/";

class MockSpdyFramerVisitor : public spdy::SpdyFramerVisitorInterface {
 public:
  // on_delete gets set in the destructor. We use this to verify that
  // the visitor gets deleted when end of stream is encountered.
  MockSpdyFramerVisitor(bool *on_delete) {
    on_delete_ = on_delete;
  }

  virtual ~MockSpdyFramerVisitor() {
    *on_delete_ = true;
  }

  MOCK_METHOD1(OnError, void(spdy::SpdyFramer*));
  MOCK_METHOD1(OnControl, void(const spdy::SpdyControlFrame*));
  MOCK_METHOD3(OnStreamFrameData,
               void(spdy::SpdyStreamId, const char*, size_t));

 private:
  bool *on_delete_;
};

class MockSpdyFramerVisitorFactory
    : public mod_spdy::SpdyFramerVisitorFactoryInterface {
 public:
  MOCK_METHOD1(Create, spdy::SpdyFramerVisitorInterface*(spdy::SpdyStreamId));
};

TEST(SpdyStreamDistributorTest, Basic) {
  InSequence seq;

  spdy::SpdyFramer distributor_framer;
  spdy::SpdyFramer generator_framer;
  MockSpdyFramerVisitorFactory factory;
  mod_spdy::SpdyStreamDistributor distributor(&distributor_framer, &factory);

  spdy::SpdyHeaderBlock headers;
  scoped_ptr<spdy::SpdySynStreamControlFrame> syn_stream_frame_1(
      generator_framer.CreateSynStream(
          1,  // stream ID
          0,  // associated stream ID
          1,  // priority
          spdy::CONTROL_FLAG_NONE,  // flags
          true,  // use compression
          &headers));

  scoped_ptr<spdy::SpdySynStreamControlFrame> syn_stream_frame_2(
      generator_framer.CreateSynStream(
          2,  // stream ID
          0,  // associated stream ID
          1,  // priority
          spdy::CONTROL_FLAG_NONE,  // flags
          true,  // use compression
          &headers));

  bool deleted_1 = false;
  MockSpdyFramerVisitor *v1 = new MockSpdyFramerVisitor(&deleted_1);
  bool deleted_2 = false;
  MockSpdyFramerVisitor *v2 = new MockSpdyFramerVisitor(&deleted_2);

  // Interleave calls from multiple streams in order to verify that
  // those calls are routed to the proper visitor.
  EXPECT_CALL(factory, Create(Eq(1))).WillOnce(Return(v1));
  EXPECT_CALL(*v1, OnControl(Eq(syn_stream_frame_1.get())));
  distributor.OnControl(syn_stream_frame_1.get());

  EXPECT_CALL(factory, Create(Eq(2))).WillOnce(Return(v2));
  EXPECT_CALL(*v2, OnControl(Eq(syn_stream_frame_2.get())));
  distributor.OnControl(syn_stream_frame_2.get());

  EXPECT_CALL(*v1, OnStreamFrameData(Eq(1), Eq(kMethod), Eq(strlen(kMethod))));
  distributor.OnStreamFrameData(1, kMethod, strlen(kMethod));

  EXPECT_CALL(*v2, OnStreamFrameData(Eq(2), Eq(kUrl), Eq(strlen(kUrl))));
  distributor.OnStreamFrameData(2, kUrl, strlen(kUrl));

  EXPECT_CALL(*v2, OnStreamFrameData(Eq(2), Eq(kMethod), Eq(strlen(kMethod))));
  distributor.OnStreamFrameData(2, kMethod, strlen(kMethod));

  EXPECT_CALL(*v1, OnStreamFrameData(Eq(1), Eq(kUrl), Eq(strlen(kUrl))));
  distributor.OnStreamFrameData(1, kUrl, strlen(kUrl));

  // Now send an end-of-stream OnStreamFrameData() call, and verify
  // that the visitor gets deleted.
  ASSERT_FALSE(deleted_1);
  EXPECT_CALL(*v1, OnStreamFrameData(Eq(1), IsNull(), Eq(0)));
  distributor.OnStreamFrameData(1, NULL, 0);
  ASSERT_TRUE(deleted_1);

  ASSERT_FALSE(deleted_2);
  EXPECT_CALL(*v2, OnStreamFrameData(Eq(2), IsNull(), Eq(0)));
  distributor.OnStreamFrameData(2, NULL, 0);
  ASSERT_TRUE(deleted_2);

  deleted_1 = false;
  v1 = new MockSpdyFramerVisitor(&deleted_1);
  syn_stream_frame_1.reset(
      generator_framer.CreateSynStream(
          1,  // stream ID
          0,  // associated stream ID
          1,  // priority
          spdy::CONTROL_FLAG_FIN,  // flags
          true,  // use compression
          &headers));

  // At this point, streams 1 and 2 should have been removed from the
  // SpdyStreamDistributor. So a new SYN frame with stream id 1 should
  // trigger a new call to Create(). Since this SYN frame is also a
  // FIN frame, we expect it to dispatch to the visitor and then
  // immediately delete the visitor.
  EXPECT_CALL(factory, Create(Eq(1))).WillOnce(Return(v1));
  EXPECT_CALL(*v1, OnControl(Eq(syn_stream_frame_1.get())));
  ASSERT_FALSE(deleted_1);
  distributor.OnControl(syn_stream_frame_1.get());
  ASSERT_TRUE(deleted_1);
}

}  // namespace
