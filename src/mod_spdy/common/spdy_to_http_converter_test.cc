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
#include "base/string_number_conversions.h"  // for IntToString
#include "mod_spdy/common/http_stream_visitor_interface.h"
#include "mod_spdy/common/spdy_to_http_converter.h"
#include "net/spdy/spdy_framer.h"
#include "net/spdy/spdy_frame_builder.h"
#include "net/spdy/spdy_protocol.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

using testing::Eq;
using testing::InSequence;
using testing::Sequence;
using testing::StrEq;

const char *kMethod = "GET";
const char *kScheme = "http";
const char *kHost = "www.example.com";
const char *kPath = "/";
const char *kVersion = "HTTP/1.1";
const char kMultiValue[] = "this\0is\0\0\0four\0\0headers";

class MockHttpStreamVisitor: public mod_spdy::HttpStreamVisitorInterface {
 public:
  MOCK_METHOD5(OnStatusLine, void(const char *, const char *,
                                  const char *, const char *, const char *));
  MOCK_METHOD2(OnHeader, void(const char *, const char *));
  MOCK_METHOD0(OnHeadersComplete, void());
  MOCK_METHOD2(OnBody, void(const char *, size_t));
  MOCK_METHOD0(OnComplete, void());
  MOCK_METHOD0(OnTerminate, void());
};

class SpdyToHttpConverterTest : public testing::Test {
 public:
  SpdyToHttpConverterTest()
      : visitor_(),
        converter_framer_(),
        generator_framer_(),
        converter_(&converter_framer_, &visitor_),
        headers_() {
  }

 protected:
  void AddRequiredHeaders() {
    headers_["method"] = kMethod;
    headers_["scheme"] = kScheme;
    headers_["host"] = kHost;
    headers_["url"] = kPath;
    headers_["version"] = kVersion;
  }

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
    ASSERT_FALSE(converter_.HasError());
#ifdef NDEBUG
    converter_.OnControl(frame);
    ASSERT_TRUE(converter_.HasError());
#else
    EXPECT_DEATH(converter_.OnControl(frame), "");
#endif
  }

  void AssertNoErrorOnControl(spdy::SpdyControlFrame *frame) {
    ASSERT_FALSE(converter_.HasError());
    converter_.OnControl(frame);
    ASSERT_FALSE(converter_.HasError());
  }

  MockHttpStreamVisitor visitor_;
  spdy::SpdyFramer converter_framer_;
  spdy::SpdyFramer generator_framer_;
  mod_spdy::SpdyToHttpConverter converter_;
  spdy::SpdyHeaderBlock headers_;
};

TEST_F(SpdyToHttpConverterTest, OnError) {
  ASSERT_FALSE(converter_.HasError());
#ifdef NDEBUG
  converter_.OnError(&converter_framer_);
  ASSERT_TRUE(converter_.HasError());
#else
  EXPECT_DEATH(converter_.OnError(&converter_framer_), "");
#endif
}

TEST_F(SpdyToHttpConverterTest, OnStreamFrameData) {
  EXPECT_CALL(visitor_, OnBody(Eq(kMultiValue), Eq(sizeof(kMultiValue))));
  converter_.OnStreamFrameData(1, kMultiValue, sizeof(kMultiValue));
  testing::Mock::VerifyAndClearExpectations(&visitor_);

  EXPECT_CALL(visitor_, OnComplete());
  converter_.OnStreamFrameData(1, NULL, 0);
  testing::Mock::VerifyAndClearExpectations(&visitor_);
}

// We also don't currently support some control frames.
TEST_F(SpdyToHttpConverterTest, ErrorOnSynReply) {
  scoped_ptr<spdy::SpdyControlFrame> frame(
      generator_framer_.CreateSynReply(
          1, spdy::CONTROL_FLAG_NONE, true, &headers_));
  AssertErrorOnControl(frame.get());
}

TEST_F(SpdyToHttpConverterTest, ErrorOnHeaders) {
  scoped_ptr<spdy::SpdyControlFrame> frame(CreateControlFrame(spdy::HEADERS));
  AssertErrorOnControl(frame.get());
}

TEST_F(SpdyToHttpConverterTest, BasicRstStream) {
  EXPECT_CALL(visitor_, OnTerminate());
  scoped_ptr<spdy::SpdyControlFrame> frame(
      generator_framer_.CreateRstStream(1, spdy::PROTOCOL_ERROR));
  AssertNoErrorOnControl(frame.get());
}

TEST_F(SpdyToHttpConverterTest, BasicNopFrame) {
  scoped_ptr<spdy::SpdyControlFrame> frame(generator_framer_.CreateNopFrame());
  AssertNoErrorOnControl(frame.get());
}

TEST_F(SpdyToHttpConverterTest, MultiFrameStream) {
  // We expect all calls to happen in the specified order.
  InSequence seq;

  AddRequiredHeaders();

  EXPECT_CALL(visitor_,
              OnStatusLine(StrEq(kMethod),
                           StrEq(kScheme),
                           StrEq(kHost),
                           StrEq(kPath),
                           StrEq(kVersion)));

  EXPECT_CALL(visitor_,
              OnHeader(StrEq("x-spdy-stream-id"),
                       StrEq("1")));

  EXPECT_CALL(visitor_, OnHeadersComplete());

  scoped_ptr<spdy::SpdySynStreamControlFrame> syn_stream_frame(
      generator_framer_.CreateSynStream(
          1,  // stream ID
          0,  // associated stream ID
          1,  // priority
          spdy::CONTROL_FLAG_NONE,  // flags
          true,  // use compression
          &headers_));
  converter_.OnControl(syn_stream_frame.get());

  EXPECT_CALL(visitor_, OnBody(Eq(kMethod), Eq(strlen(kMethod))));
  converter_.OnStreamFrameData(1, kMethod, strlen(kMethod));

  EXPECT_CALL(visitor_, OnBody(Eq(kHost), Eq(strlen(kHost))));
  converter_.OnStreamFrameData(1, kHost, strlen(kHost));

  EXPECT_CALL(visitor_, OnComplete());
  converter_.OnStreamFrameData(1, NULL, 0);
}

// Send multiple SYN frames through the converter, to exercise the
// inter-frame compression context.
TEST_F(SpdyToHttpConverterTest, MultipleSynFrames) {
  // We expect all calls to happen in the specified order.
  InSequence seq;

  AddRequiredHeaders();

  for (int i = 1; i < 10; ++i) {
    scoped_ptr<spdy::SpdySynStreamControlFrame> syn_frame(
        generator_framer_.CreateSynStream(
            i,  // stream ID
            0,  // associated stream ID
            1,  // priority
            spdy::CONTROL_FLAG_FIN,  // flags
            true,  // use compression
            &headers_));

    EXPECT_CALL(visitor_,
              OnStatusLine(StrEq(kMethod),
                           StrEq(kScheme),
                           StrEq(kHost),
                           StrEq(kPath),
                           StrEq(kVersion)));

    EXPECT_CALL(visitor_,
                OnHeader(StrEq("x-spdy-stream-id"),
                         StrEq(base::IntToString(i))));

    EXPECT_CALL(visitor_, OnHeadersComplete());

    EXPECT_CALL(visitor_, OnComplete());

    // Trigger the calls to the mock object by passing the frame to the
    // converter.
    converter_.OnControl(syn_frame.get());

    testing::Mock::VerifyAndClearExpectations(&visitor_);
  }
}

TEST_F(SpdyToHttpConverterTest, SynFrameWithHeaders) {
  AddRequiredHeaders();
  headers_["foo"] = "bar";
  headers_["spdy"] = "spdy";

  // Create a multi-valued header to verify that it's processed
  // properly.
  std::string multi_values(kMultiValue, sizeof(kMultiValue));
  headers_["multi"] = multi_values;

  // Also make sure "junk" headers get skipped over.
  headers_["empty"] = std::string("\0\0\0", 3);

  scoped_ptr<spdy::SpdySynStreamControlFrame> syn_frame(
      generator_framer_.CreateSynStream(
          1,  // stream ID
          0,  // associated stream ID
          1,  // priority
          spdy::CONTROL_FLAG_FIN,  // flags
          true,  // use compression
          &headers_));

  // We expect a call to OnStatusLine(), followed by two calls to
  // OnHeader() (the order of the calls to OnHeader() is
  // non-deterministic so we put each in its own Sequence), followed
  // by a final call to OnHeadersComplete() and OnComplete().
  Sequence s1, s2, s3;
  EXPECT_CALL(visitor_,
              OnStatusLine(StrEq(kMethod),
                           StrEq(kScheme),
                           StrEq(kHost),
                           StrEq(kPath),
                           StrEq(kVersion)))
      .InSequence(s1, s2, s3);

  EXPECT_CALL(visitor_,
              OnHeader(StrEq("x-spdy-stream-id"),
                       StrEq("1")))
      .InSequence(s1);

  EXPECT_CALL(visitor_,
              OnHeader(StrEq("foo"),
                       StrEq("bar")))
      .InSequence(s1);

  EXPECT_CALL(visitor_,
              OnHeader(StrEq("spdy"),
                       StrEq("spdy")))
      .InSequence(s2);

  EXPECT_CALL(visitor_,
              OnHeader(StrEq("multi"),
                       StrEq("this")))
      .InSequence(s3);

  EXPECT_CALL(visitor_,
              OnHeader(StrEq("multi"),
                       StrEq("is")))
      .InSequence(s3);

  EXPECT_CALL(visitor_,
              OnHeader(StrEq("multi"),
                       StrEq("four")))
      .InSequence(s3);

  EXPECT_CALL(visitor_,
              OnHeader(StrEq("multi"),
                       StrEq("headers")))
      .InSequence(s3);

  EXPECT_CALL(visitor_, OnHeadersComplete()).InSequence(s1, s2, s3);

  EXPECT_CALL(visitor_, OnComplete()).InSequence(s1, s2, s3);

  // Trigger the calls to the mock object by passing the frame to the
  // converter.
  converter_.OnControl(syn_frame.get());
}

}  // namespace
