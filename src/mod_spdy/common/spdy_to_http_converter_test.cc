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
#include "base/string_util.h"  // for IntToString
#include "mod_spdy/common/http_stream_visitor_interface.h"
#include "mod_spdy/common/spdy_to_http_converter.h"
#include "net/spdy/spdy_framer.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

using testing::Eq;
using testing::InSequence;
using testing::Sequence;
using testing::StrEq;

const char *kMethod = "GET";
const char *kUrl = "http://www.example.com/";
const char *kVersion = "HTTP/1.1";
const char kMultiValue[] = "this\0is\0\0\0four\0\0headers";

class MockHttpStreamVisitor: public mod_spdy::HttpStreamVisitorInterface {
 public:
  MOCK_METHOD3(OnStatusLine, void(const char *, const char *, const char *));
  MOCK_METHOD2(OnHeader, void(const char *, const char *));
  MOCK_METHOD0(OnHeadersComplete, void());
  MOCK_METHOD2(OnBody, void(const char *, size_t));
  MOCK_METHOD0(OnComplete, void());
};

// Make it clear that we do not currently handle most callbacks.
TEST(SpdyToHttpConverterTest, UnsupportedCallbacks) {
  MockHttpStreamVisitor visitor;
  spdy::SpdyFramer framer;
  mod_spdy::SpdyToHttpConverter converter(&framer, &visitor);
  EXPECT_DEATH(converter.OnError(NULL), "");
}

TEST(SpdyToHttpConverterTest, OnStreamFrameData) {
  MockHttpStreamVisitor visitor;
  spdy::SpdyFramer framer;
  mod_spdy::SpdyToHttpConverter converter(&framer, &visitor);

  EXPECT_CALL(visitor, OnBody(Eq(kMultiValue), Eq(sizeof(kMultiValue))));
  converter.OnStreamFrameData(1, kMultiValue, sizeof(kMultiValue));
  testing::Mock::VerifyAndClearExpectations(&visitor);

  EXPECT_CALL(visitor, OnComplete());
  converter.OnStreamFrameData(1, NULL, 0);
  testing::Mock::VerifyAndClearExpectations(&visitor);
}

// We also don't currently support most control frames.
TEST(SpdyToHttpConverterTest, UnsupportedControlFrames) {
  MockHttpStreamVisitor visitor;
  spdy::SpdyFramer converter_framer;
  spdy::SpdyFramer generator_framer;
  mod_spdy::SpdyToHttpConverter converter(&converter_framer, &visitor);
  spdy::SpdyHeaderBlock headers;
  headers["method"] = kMethod;
  headers["url"] = kUrl;
  headers["version"] = kVersion;

  // We don't handle syn reply.
  scoped_ptr<spdy::SpdySynReplyControlFrame> syn_reply_frame(
      generator_framer.CreateSynReply(
          1, spdy::CONTROL_FLAG_NONE, true, &headers));
  EXPECT_DEATH(converter.OnControl(syn_reply_frame.get()), "");

  // We don't handle fin stream.
  scoped_ptr<spdy::SpdyRstStreamControlFrame> rst_stream_frame(
      generator_framer.CreateRstStream(1, 0));
  EXPECT_DEATH(converter.OnControl(rst_stream_frame.get()), "");

  // We don't handle nop.
  scoped_ptr<spdy::SpdyControlFrame> nop_frame(
      generator_framer.CreateNopFrame());
  EXPECT_DEATH(converter.OnControl(nop_frame.get()), "");
}

TEST(SpdyToHttpConverter, MultiFrameStream) {
  InSequence seq;

  MockHttpStreamVisitor visitor;
  spdy::SpdyFramer converter_framer;
  spdy::SpdyFramer generator_framer;
  mod_spdy::SpdyToHttpConverter converter(&converter_framer, &visitor);
  spdy::SpdyHeaderBlock headers;
  headers["method"] = kMethod;
  headers["url"] = kUrl;
  headers["version"] = kVersion;

  EXPECT_CALL(visitor,
              OnStatusLine(StrEq(kMethod),
                           StrEq(kUrl),
                           StrEq(kVersion)));

  EXPECT_CALL(visitor,
              OnHeader(StrEq("x-spdy-stream-id"),
                       StrEq("1")));

  EXPECT_CALL(visitor, OnHeadersComplete());

  EXPECT_CALL(visitor, OnBody(Eq(kMethod), Eq(strlen(kMethod))));

  EXPECT_CALL(visitor, OnBody(Eq(kUrl), Eq(strlen(kUrl))));

  EXPECT_CALL(visitor, OnComplete());

  scoped_ptr<spdy::SpdySynStreamControlFrame> syn_stream_frame(
      generator_framer.CreateSynStream(
          1,  // stream ID
          0,  // associated stream ID
          1,  // priority
          spdy::CONTROL_FLAG_NONE,  // flags
          true,  // use compression
          &headers));
  converter.OnControl(syn_stream_frame.get());

  converter.OnStreamFrameData(1, kMethod, strlen(kMethod));
  converter.OnStreamFrameData(1, kUrl, strlen(kUrl));
  converter.OnStreamFrameData(1, NULL, 0);
}

// Send multiple SYN frames through the converter, to exercise the
// inter-frame compression context.
TEST(SpdyToHttpConverterTest, MultipleSynFrames) {
  InSequence seq;

  MockHttpStreamVisitor visitor;
  spdy::SpdyFramer converter_framer;
  spdy::SpdyFramer generator_framer;
  mod_spdy::SpdyToHttpConverter converter(&converter_framer, &visitor);

  spdy::SpdyHeaderBlock headers;
  headers["method"] = kMethod;
  headers["url"] = kUrl;
  headers["version"] = kVersion;

  for (int i = 0; i < 10; ++i) {
    scoped_ptr<spdy::SpdySynStreamControlFrame> syn_frame(
        generator_framer.CreateSynStream(
            i,  // stream ID
            0,  // associated stream ID
            1,  // priority
            spdy::CONTROL_FLAG_FIN,  // flags
            true,  // use compression
            &headers));

    EXPECT_CALL(visitor,
                OnStatusLine(StrEq(kMethod),
                             StrEq(kUrl),
                             StrEq(kVersion)));

    EXPECT_CALL(visitor,
                OnHeader(StrEq("x-spdy-stream-id"),
                         StrEq(IntToString(i))));

    EXPECT_CALL(visitor, OnHeadersComplete());

    EXPECT_CALL(visitor, OnComplete());

    // Trigger the calls to the mock object by passing the frame to the
    // converter.
    converter.OnControl(syn_frame.get());

    testing::Mock::VerifyAndClearExpectations(&visitor);
  }
}

TEST(SpdyToHttpConverterTest, SynFrameWithHeaders) {
  MockHttpStreamVisitor visitor;
  spdy::SpdyFramer converter_framer;
  spdy::SpdyFramer generator_framer;
  mod_spdy::SpdyToHttpConverter converter(&converter_framer, &visitor);
  spdy::SpdyHeaderBlock headers;
  headers["method"] = kMethod;
  headers["url"] = kUrl;
  headers["version"] = kVersion;
  headers["foo"] = "bar";
  headers["spdy"] = "spdy";

  // Create a multi-valued header to verify that it's processed
  // properly.
  std::string multi_values(kMultiValue, sizeof(kMultiValue));
  headers["multi"] = multi_values;

  // Also make sure "junk" headers get skipped over.
  headers["empty"] = std::string("\0\0\0", 3);
  headers["empty2"] = "";

  scoped_ptr<spdy::SpdySynStreamControlFrame> syn_frame(
      generator_framer.CreateSynStream(
          1,  // stream ID
          0,  // associated stream ID
          1,  // priority
          spdy::CONTROL_FLAG_FIN,  // flags
          true,  // use compression
          &headers));

  // We expect a call to OnStatusLine(), followed by two calls to
  // OnHeader() (the order of the calls to OnHeader() is
  // non-deterministic so we put each in its own Sequence), followed
  // by a final call to OnHeadersComplete() and OnComplete().
  Sequence s1, s2, s3;
  EXPECT_CALL(visitor,
              OnStatusLine(StrEq(kMethod),
                           StrEq(kUrl),
                           StrEq(kVersion)))
      .InSequence(s1, s2, s3);

  EXPECT_CALL(visitor,
              OnHeader(StrEq("x-spdy-stream-id"),
                       StrEq("1")))
      .InSequence(s1);

  EXPECT_CALL(visitor,
              OnHeader(StrEq("foo"),
                       StrEq("bar")))
      .InSequence(s1);

  EXPECT_CALL(visitor,
              OnHeader(StrEq("spdy"),
                       StrEq("spdy")))
      .InSequence(s2);

  EXPECT_CALL(visitor,
              OnHeader(StrEq("multi"),
                       StrEq("this")))
      .InSequence(s3);

  EXPECT_CALL(visitor,
              OnHeader(StrEq("multi"),
                       StrEq("is")))
      .InSequence(s3);

  EXPECT_CALL(visitor,
              OnHeader(StrEq("multi"),
                       StrEq("four")))
      .InSequence(s3);

  EXPECT_CALL(visitor,
              OnHeader(StrEq("multi"),
                       StrEq("headers")))
      .InSequence(s3);

  EXPECT_CALL(visitor, OnHeadersComplete()).InSequence(s1, s2, s3);

  EXPECT_CALL(visitor, OnComplete()).InSequence(s1, s2, s3);

  // Trigger the calls to the mock object by passing the frame to the
  // converter.
  converter.OnControl(syn_frame.get());
}

}  // namespace
