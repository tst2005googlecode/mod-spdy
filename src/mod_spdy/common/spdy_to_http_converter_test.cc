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

#include "mod_spdy/common/http_stream_visitor_interface.h"
#include "mod_spdy/common/spdy_to_http_converter.h"
#include "net/flip/flip_framer.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

using testing::Sequence;
using testing::StrEq;

const char *kMethod = "GET";
const char *kUrl = "http://www.example.com/";
const char *kVersion = "HTTP/1.1";

class MockHttpStreamVisitor: public mod_spdy::HttpStreamVisitorInterface {
 public:
  MOCK_METHOD3(OnStatusLine, void(const char *, const char *, const char *));
  MOCK_METHOD2(OnHeader, void(const char *, const char *));
  MOCK_METHOD0(OnHeadersComplete, void());
  MOCK_METHOD2(OnBody, void(const char *, size_t));
};

// Make it clear that we do not currently handle most callbacks.
TEST(SpdyToHttpConverterTest, UnsupportedCallbacks) {
  mod_spdy::SpdyToHttpConverter converter(NULL);
  EXPECT_DEATH(converter.OnError(NULL), "");
  EXPECT_DEATH(converter.OnStreamFrameData(1, NULL, 0), "");
}

// We also don't currently support most control frames.
TEST(SpdyToHttpConverterTest, UnsupportedControlFrames) {
  mod_spdy::SpdyToHttpConverter converter(NULL);
  flip::FlipFramer framer;
  flip::FlipHeaderBlock headers;

  // We don't handle syn stream unless it has CONTROL_FLAG_FIN set.
  scoped_ptr<flip::FlipSynStreamControlFrame> syn_stream_frame(
      framer.CreateSynStream(1, 1, flip::CONTROL_FLAG_NONE, true, &headers));
  EXPECT_DEATH(converter.OnControl(syn_stream_frame.get()), "");

  // We don't handle syn reply.
  scoped_ptr<flip::FlipSynReplyControlFrame> syn_reply_frame(
      framer.CreateSynReply(1, flip::CONTROL_FLAG_NONE, true, &headers));
  EXPECT_DEATH(converter.OnControl(syn_reply_frame.get()), "");

  // We don't handle fin stream.
  scoped_ptr<flip::FlipFinStreamControlFrame> fin_stream_frame(
      framer.CreateFinStream(1, 0));
  EXPECT_DEATH(converter.OnControl(fin_stream_frame.get()), "");

  // We don't handle nop.
  scoped_ptr<flip::FlipControlFrame> nop_frame(framer.CreateNopFrame());
  EXPECT_DEATH(converter.OnControl(nop_frame.get()), "");
}

TEST(SpdyToHttpConverterTest, BasicSynFrame) {
  MockHttpStreamVisitor visitor;
  mod_spdy::SpdyToHttpConverter converter(&visitor);
  flip::FlipFramer framer;
  flip::FlipHeaderBlock headers;
  headers["method"] = kMethod;
  headers["url"] = kUrl;
  headers["version"] = kVersion;
  headers["foo"] = "bar";
  headers["flip"] = "spdy";
  scoped_ptr<flip::FlipSynStreamControlFrame> syn_frame(
      framer.CreateSynStream(1, 1, flip::CONTROL_FLAG_FIN, true, &headers));

  // We expect a call to OnStatusLine(), followed by two calls to
  // OnHeader() (the order of the calls to OnHeader() is
  // non-deterministic so we put each in its own Sequence), followed
  // by a final call to OnHeadersComplete().
  Sequence s1, s2;
  EXPECT_CALL(visitor,
              OnStatusLine(StrEq(kMethod),
                           StrEq(kUrl),
                           StrEq(kVersion)))
      .InSequence(s1, s2);

  EXPECT_CALL(visitor,
              OnHeader(StrEq("foo"),
                       StrEq("bar")))
      .InSequence(s1);

  EXPECT_CALL(visitor,
              OnHeader(StrEq("flip"),
                       StrEq("spdy")))
      .InSequence(s2);

  EXPECT_CALL(visitor, OnHeadersComplete()).InSequence(s1, s2);

  // Trigger the calls to the mock object by passing the frame to the
  // converter.
  converter.OnControl(syn_frame.get());
}

}  // namespace
