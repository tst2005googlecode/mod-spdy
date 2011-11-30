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
#include "net/spdy/spdy_frame_builder.h"
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

class SpdyStreamDistributorTest : public testing::Test {
 public:
  SpdyStreamDistributorTest()
      : factory_(),
        distributor_framer_(),
        generator_framer_(),
        distributor_(&distributor_framer_, &factory_),
        headers_() {
  }

 protected:
  // Helper to create a stub control frame, until SpdyFramer supports
  // building all types of frames.
  spdy::SpdyControlFrame* CreateControlFrame(spdy::SpdyControlType type) {
    spdy::SpdyFrameBuilder frame;
    frame.WriteUInt16(spdy::kControlFlagMask | spdy::kSpdyProtocolVersion);
    frame.WriteUInt16(type);
    frame.WriteUInt32(0);  // length and flags
    return static_cast<spdy::SpdyControlFrame*>(frame.take());
  }

  void AssertErrorOnControl(spdy::SpdyControlFrame *frame) {
    ASSERT_FALSE(distributor_.HasError());
#ifdef NDEBUG
    distributor_.OnControl(frame);
    ASSERT_TRUE(distributor_.HasError());
#else
    EXPECT_DEATH(distributor_.OnControl(frame), "");
#endif
  }

  void AssertNoErrorOnControl(spdy::SpdyControlFrame *frame) {
    ASSERT_FALSE(distributor_.HasError());
    distributor_.OnControl(frame);
    ASSERT_FALSE(distributor_.HasError());
  }

  MockSpdyFramerVisitorFactory factory_;
  spdy::SpdyFramer distributor_framer_;
  spdy::SpdyFramer generator_framer_;
  mod_spdy::SpdyStreamDistributor distributor_;
  spdy::SpdyHeaderBlock headers_;
};

TEST_F(SpdyStreamDistributorTest, ErrorOnSettings) {
  scoped_ptr<spdy::SpdyControlFrame> frame(CreateControlFrame(spdy::SETTINGS));
  AssertErrorOnControl(frame.get());
}

TEST_F(SpdyStreamDistributorTest, ErrorOnGoAway) {
  scoped_ptr<spdy::SpdyControlFrame> frame(CreateControlFrame(spdy::GOAWAY));
  AssertErrorOnControl(frame.get());
}

TEST_F(SpdyStreamDistributorTest, BasicNopFrame) {
  scoped_ptr<spdy::SpdyControlFrame> frame(generator_framer_.CreateNopFrame());
  AssertNoErrorOnControl(frame.get());
}

TEST_F(SpdyStreamDistributorTest, Basic) {
  InSequence seq;
  scoped_ptr<spdy::SpdySynStreamControlFrame> syn_stream_frame_1(
      generator_framer_.CreateSynStream(
          1,  // stream ID
          0,  // associated stream ID
          1,  // priority
          spdy::CONTROL_FLAG_NONE,  // flags
          true,  // use compression
          &headers_));

  scoped_ptr<spdy::SpdySynStreamControlFrame> syn_stream_frame_2(
      generator_framer_.CreateSynStream(
          2,  // stream ID
          0,  // associated stream ID
          1,  // priority
          spdy::CONTROL_FLAG_NONE,  // flags
          true,  // use compression
          &headers_));

  bool deleted_1 = false;
  MockSpdyFramerVisitor *v1 = new MockSpdyFramerVisitor(&deleted_1);
  bool deleted_2 = false;
  MockSpdyFramerVisitor *v2 = new MockSpdyFramerVisitor(&deleted_2);

  // Interleave calls from multiple streams in order to verify that
  // those calls are routed to the proper visitor.
  EXPECT_CALL(factory_, Create(Eq(1))).WillOnce(Return(v1));
  EXPECT_CALL(*v1, OnControl(Eq(syn_stream_frame_1.get())));
  distributor_.OnControl(syn_stream_frame_1.get());

  EXPECT_CALL(factory_, Create(Eq(2))).WillOnce(Return(v2));
  EXPECT_CALL(*v2, OnControl(Eq(syn_stream_frame_2.get())));
  distributor_.OnControl(syn_stream_frame_2.get());

  EXPECT_CALL(*v1, OnStreamFrameData(Eq(1), Eq(kMethod), Eq(strlen(kMethod))));
  distributor_.OnStreamFrameData(1, kMethod, strlen(kMethod));

  EXPECT_CALL(*v2, OnStreamFrameData(Eq(2), Eq(kUrl), Eq(strlen(kUrl))));
  distributor_.OnStreamFrameData(2, kUrl, strlen(kUrl));

  EXPECT_CALL(*v2, OnStreamFrameData(Eq(2), Eq(kMethod), Eq(strlen(kMethod))));
  distributor_.OnStreamFrameData(2, kMethod, strlen(kMethod));

  EXPECT_CALL(*v1, OnStreamFrameData(Eq(1), Eq(kUrl), Eq(strlen(kUrl))));
  distributor_.OnStreamFrameData(1, kUrl, strlen(kUrl));

  // Now send an end-of-stream OnStreamFrameData() call, and verify
  // that the visitor gets deleted.
  ASSERT_FALSE(deleted_1);
  EXPECT_CALL(*v1, OnStreamFrameData(Eq(1), IsNull(), Eq(0)));
  distributor_.OnStreamFrameData(1, NULL, 0);
  ASSERT_TRUE(deleted_1);

  ASSERT_FALSE(deleted_2);
  EXPECT_CALL(*v2, OnStreamFrameData(Eq(2), IsNull(), Eq(0)));
  distributor_.OnStreamFrameData(2, NULL, 0);
  ASSERT_TRUE(deleted_2);

  deleted_1 = false;
  v1 = new MockSpdyFramerVisitor(&deleted_1);
  syn_stream_frame_1.reset(
      generator_framer_.CreateSynStream(
          1,  // stream ID
          0,  // associated stream ID
          1,  // priority
          spdy::CONTROL_FLAG_FIN,  // flags
          true,  // use compression
          &headers_));

  // At this point, streams 1 and 2 should have been removed from the
  // SpdyStreamDistributor. So a new SYN frame with stream id 1 should
  // trigger a new call to Create(). Since this SYN frame is also a
  // FIN frame, we expect it to dispatch to the visitor and then
  // immediately delete the visitor.
  EXPECT_CALL(factory_, Create(Eq(1))).WillOnce(Return(v1));
  EXPECT_CALL(*v1, OnControl(Eq(syn_stream_frame_1.get())));
  ASSERT_FALSE(deleted_1);
  distributor_.OnControl(syn_stream_frame_1.get());
  ASSERT_TRUE(deleted_1);
}

}  // namespace
