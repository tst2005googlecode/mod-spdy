// Copyright 2011 Google Inc. All Rights Reserved.
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

#include "mod_spdy/common/http_response_parser.h"

#include "base/string_piece.h"
#include "mod_spdy/common/http_response_visitor_interface.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using mod_spdy::HttpResponseParser;
using testing::Eq;
using testing::InSequence;

namespace {

class MockHttpResponseVisitor: public mod_spdy::HttpResponseVisitorInterface {
 public:
  MOCK_METHOD3(OnStatusLine, void(const base::StringPiece&,
                                  const base::StringPiece&,
                                  const base::StringPiece&));
  MOCK_METHOD2(OnLeadingHeader, void(const base::StringPiece&,
                                     const base::StringPiece&));
  MOCK_METHOD0(OnLeadingHeadersComplete, void());
  MOCK_METHOD1(OnData, void(const base::StringPiece&));
  MOCK_METHOD0(OnComplete, void());
};

class HttpResponseParserTest : public testing::Test {
 public:
  HttpResponseParserTest() : parser_(&visitor_) {}

 protected:
  MockHttpResponseVisitor visitor_;
  HttpResponseParser parser_;
};

TEST_F(HttpResponseParserTest, SimpleWithContentLength) {
  InSequence seq;
  EXPECT_CALL(visitor_, OnStatusLine(Eq("HTTP/1.1"), Eq("200"), Eq("OK")));
  EXPECT_CALL(visitor_, OnLeadingHeader(Eq("Content-Length"), Eq("14")));
  EXPECT_CALL(visitor_, OnLeadingHeader(Eq("Content-Type"), Eq("text/plain")));
  EXPECT_CALL(visitor_, OnLeadingHeader(Eq("X-Whatever"), Eq("foobar")));
  EXPECT_CALL(visitor_, OnLeadingHeadersComplete());
  EXPECT_CALL(visitor_, OnData(Eq("Hello, world!\n")));
  EXPECT_CALL(visitor_, OnComplete());

  ASSERT_TRUE(parser_.ProcessInput(
      "HTTP/1.1 200 OK\r\n"
      "Content-Length: 14\r\n"
      "Content-Type:      text/plain\r\n"
      "X-Whatever:foobar\r\n"
      "\r\n"
      "Hello, world!\n"
      "\r\n"));
}

TEST_F(HttpResponseParserTest, SimpleWithChunkedData) {
  InSequence seq;
  EXPECT_CALL(visitor_, OnStatusLine(Eq("HTTP/1.1"), Eq("200"), Eq("OK")));
  EXPECT_CALL(visitor_, OnLeadingHeader(
      Eq("Transfer-Encoding"), Eq("chunked")));
  EXPECT_CALL(visitor_, OnLeadingHeader(Eq("Content-Type"), Eq("text/plain")));
  EXPECT_CALL(visitor_, OnLeadingHeadersComplete());
  EXPECT_CALL(visitor_, OnData(Eq("Hello, world!\n")));
  EXPECT_CALL(visitor_, OnData(Eq("It sure is good to see you today.")));
  EXPECT_CALL(visitor_, OnComplete());

  ASSERT_TRUE(parser_.ProcessInput(
      "HTTP/1.1 200 OK\r\n"
      "Transfer-Encoding: chunked\r\n"
      "Content-Type: text/plain\r\n"
      "\r\n"
      "E\r\n"
      "Hello, world!\n\r\n"
      "21; some-random-chunk-extension\r\n"
      "It sure is good to see you today.\r\n"
      "0\r\n"
      "\r\n"));
}

// Check that Transfer-Encoding: chunked supersedes Content-Length.
TEST_F(HttpResponseParserTest, ContentLengthAndTransferEncoding) {
  InSequence seq;
  EXPECT_CALL(visitor_, OnStatusLine(Eq("HTTP/1.1"), Eq("200"), Eq("OK")));
  EXPECT_CALL(visitor_, OnLeadingHeader(Eq("Content-Length"), Eq("3")));
  EXPECT_CALL(visitor_, OnLeadingHeader(
      Eq("Transfer-Encoding"), Eq("chunked")));
  EXPECT_CALL(visitor_, OnLeadingHeadersComplete());
  EXPECT_CALL(visitor_, OnData(Eq("Hello,")));
  EXPECT_CALL(visitor_, OnData(Eq(" world!\n")));
  EXPECT_CALL(visitor_, OnComplete());

  ASSERT_TRUE(parser_.ProcessInput(
      "HTTP/1.1 200 OK\r\n"
      "Content-Length: 3\r\n"
      "Transfer-Encoding: chunked\r\n"
      "\r\n"
      "6\r\n"
      "Hello,\r\n"
      "8\r\n"
      " world!\n\r\n"
      "0\r\n"
      "\r\n"));
}

// Check that Transfer-Encoding: chunked supersedes Content-Length even if
// Content-Length comes later.
TEST_F(HttpResponseParserTest, TransferEncodingAndContentLength) {
  InSequence seq;
  EXPECT_CALL(visitor_, OnStatusLine(Eq("HTTP/1.1"), Eq("200"), Eq("OK")));
  EXPECT_CALL(visitor_, OnLeadingHeader(
      Eq("Transfer-Encoding"), Eq("chunked")));
  EXPECT_CALL(visitor_, OnLeadingHeader(Eq("Content-Length"), Eq("3")));
  EXPECT_CALL(visitor_, OnLeadingHeadersComplete());
  EXPECT_CALL(visitor_, OnData(Eq("Hello,")));
  EXPECT_CALL(visitor_, OnData(Eq(" world!\n")));
  EXPECT_CALL(visitor_, OnComplete());

  ASSERT_TRUE(parser_.ProcessInput(
      "HTTP/1.1 200 OK\r\n"
      "Transfer-Encoding: chunked\r\n"
      "Content-Length: 3\r\n"
      "\r\n"
      "6\r\n"
      "Hello,\r\n"
      "8\r\n"
      " world!\n\r\n"
      "0\r\n"
      "\r\n"));
}

TEST_F(HttpResponseParserTest, NoBodyData) {
  InSequence seq;
  EXPECT_CALL(visitor_, OnStatusLine(Eq("HTTP/1.1"), Eq("301"),
                                     Eq("Moved permenantly")));
  EXPECT_CALL(visitor_, OnLeadingHeader(Eq("X-Empty"), Eq("")));
  EXPECT_CALL(visitor_, OnLeadingHeader(Eq("Location"), Eq("/foo/bar.html")));
  EXPECT_CALL(visitor_, OnLeadingHeadersComplete());
  EXPECT_CALL(visitor_, OnComplete());

  ASSERT_TRUE(parser_.ProcessInput(
      "HTTP/1.1  301  Moved permenantly\r\n"
      "X-Empty:\r\n"
      "Location: /foo/bar.html\r\n"
      "\r\n"));
}

TEST_F(HttpResponseParserTest, NoStatusPhrase) {
  InSequence seq;
  EXPECT_CALL(visitor_, OnStatusLine(Eq("HTTP/1.1"), Eq("123"), Eq("")));
  EXPECT_CALL(visitor_, OnLeadingHeadersComplete());
  EXPECT_CALL(visitor_, OnComplete());

  ASSERT_TRUE(parser_.ProcessInput(
      "HTTP/1.1 123\r\n"
      "\r\n"));
}

TEST_F(HttpResponseParserTest, HeadersBrokenAcrossLines) {
  InSequence seq;
  EXPECT_CALL(visitor_, OnStatusLine(Eq("HTTP/1.1"), Eq("301"),
                                     Eq("Moved permenantly")));
  EXPECT_CALL(visitor_, OnLeadingHeader(
      Eq("X-NextLine"), Eq("Alas, this is legal HTTP.")));
  EXPECT_CALL(visitor_, OnLeadingHeader(Eq("Location"), Eq("/foo/bar.html")));
  EXPECT_CALL(visitor_, OnLeadingHeader(
      Eq("X-ThreeLines"), Eq("foo    bar  baz     quux")));
  EXPECT_CALL(visitor_, OnLeadingHeadersComplete());
  EXPECT_CALL(visitor_, OnComplete());

  ASSERT_TRUE(parser_.ProcessInput(
      "HTTP/1.1 301 Moved permenantly\r\n"
      "X-NextLine:\r\n"
      "\tAlas, this is legal HTTP.\r\n"
      "Location: /foo/bar.html\r\n"
      "X-ThreeLines: foo\r\n"
      "    bar  baz \r\n"
      "    quux\r\n"
      "\r\n"));
}

TEST_F(HttpResponseParserTest, DividedUpIntoPieces) {
  InSequence seq;
  EXPECT_CALL(visitor_, OnStatusLine(Eq("HTTP/1.1"), Eq("418"),
                                     Eq("I'm a teapot")));
  EXPECT_CALL(visitor_, OnLeadingHeader(
      Eq("tRaNsFeR-EnCoDiNg"), Eq("chunked")));
  EXPECT_CALL(visitor_, OnLeadingHeader(Eq("X-TwoLines"), Eq("foo  bar")));
  EXPECT_CALL(visitor_, OnLeadingHeader(Eq("Content-Type"), Eq("text/plain")));
  EXPECT_CALL(visitor_, OnLeadingHeadersComplete());
  EXPECT_CALL(visitor_, OnData(Eq("Hello,")));
  EXPECT_CALL(visitor_, OnData(Eq(" world!\n")));
  EXPECT_CALL(visitor_, OnData(Eq("It sure")));
  EXPECT_CALL(visitor_, OnData(Eq(" is good to see you today.")));
  EXPECT_CALL(visitor_, OnComplete());

  ASSERT_TRUE(parser_.ProcessInput("HTTP/1.1 418 I'm"));
  ASSERT_TRUE(parser_.ProcessInput(" a teapot\r\ntRaNsFeR-EnCoDiNg:"));
  ASSERT_TRUE(parser_.ProcessInput("chunked\r\n"));
  ASSERT_TRUE(parser_.ProcessInput("X-TwoLines:\tfoo "));
  ASSERT_TRUE(parser_.ProcessInput("\r\n"));
  ASSERT_TRUE(parser_.ProcessInput(" bar"));
  ASSERT_TRUE(parser_.ProcessInput("\r\nContent-Type: text/plain"));
  ASSERT_TRUE(parser_.ProcessInput("\r\n\r\nE"));
  ASSERT_TRUE(parser_.ProcessInput("\r\n"));
  ASSERT_TRUE(parser_.ProcessInput("Hello,"));
  ASSERT_TRUE(parser_.ProcessInput(" world!\n"));
  ASSERT_TRUE(parser_.ProcessInput("\r\n"));
  ASSERT_TRUE(parser_.ProcessInput("21; some-random-"));
  ASSERT_TRUE(parser_.ProcessInput("chunk-extension\r\nIt sure"));
  ASSERT_TRUE(parser_.ProcessInput(" is good to see you today.\r\n0"));
  ASSERT_TRUE(parser_.ProcessInput("0\r\n\r\n"));
}

}  // namespace
