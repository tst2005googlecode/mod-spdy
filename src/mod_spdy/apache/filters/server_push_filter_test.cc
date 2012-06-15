// Copyright 2012 Google Inc.
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

#include "mod_spdy/apache/filters/server_push_filter.h"

#include <string>

#include "httpd.h"
#include "apr_buckets.h"
#include "apr_tables.h"
#include "util_filter.h"

#include "base/string_piece.h"
#include "mod_spdy/apache/pool_util.h"
#include "mod_spdy/common/protocol_util.h"
#include "mod_spdy/common/spdy_frame_priority_queue.h"
#include "mod_spdy/common/spdy_stream.h"
#include "net/spdy/buffered_spdy_framer.h"
#include "net/spdy/spdy_framer.h"
#include "net/spdy/spdy_protocol.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using testing::_;
using testing::Eq;
using testing::Return;

namespace {

const char* const kRefererUrl = "https://www.example.com/index.html";

class MockSpdyServerPushInterface : public mod_spdy::SpdyServerPushInterface {
 public:
    MOCK_METHOD3(StartServerPush,
                 mod_spdy::SpdyServerPushInterface::PushStatus(
                     net::SpdyStreamId associated_stream_id,
                     net::SpdyPriority priority,
                     const net::SpdyHeaderBlock& request_headers));
};

class ServerPushFilterTest : public testing::TestWithParam<int> {
 public:
  ServerPushFilterTest()
      : framer_(GetParam()),
        buffered_framer_(GetParam()),
        connection_(static_cast<conn_rec*>(
          apr_pcalloc(local_.pool(), sizeof(conn_rec)))),
        request_(static_cast<request_rec*>(
          apr_pcalloc(local_.pool(), sizeof(request_rec)))),
        ap_filter_(static_cast<ap_filter_t*>(
            apr_pcalloc(local_.pool(), sizeof(ap_filter_t)))),
        bucket_alloc_(apr_bucket_alloc_create(local_.pool())),
        brigade_(apr_brigade_create(local_.pool(), bucket_alloc_)) {
    // Set up our Apache data structures.  To keep things simple, we set only
    // the bare minimum of necessary fields, and rely on apr_pcalloc to zero
    // all others.
    connection_->pool = local_.pool();
    request_->pool = local_.pool();
    request_->connection = connection_;
    request_->headers_in = apr_table_make(local_.pool(), 5);
    request_->headers_out = apr_table_make(local_.pool(), 5);
    request_->protocol = const_cast<char*>("HTTP/1.1");
    request_->unparsed_uri = const_cast<char*>(kRefererUrl);
    ap_filter_->c = connection_;
    ap_filter_->r = request_;
  }

  virtual void SetUp() {
    ON_CALL(pusher_, StartServerPush(_, _, _)).WillByDefault(
        Return(mod_spdy::SpdyServerPushInterface::PUSH_STARTED));
    apr_table_setn(request_->headers_in, mod_spdy::http::kHost,
                   "www.example.com");
  }

 protected:
  void WriteBrigade(mod_spdy::ServerPushFilter* filter) {
    EXPECT_EQ(APR_SUCCESS, filter->Write(ap_filter_, brigade_));
  }

  const char* status_header_name() const {
    return (framer_.protocol_version() < 3 ? mod_spdy::spdy::kSpdy2Status :
            mod_spdy::spdy::kSpdy3Status);
  }

  const char* version_header_name() const {
    return (framer_.protocol_version() < 3 ? mod_spdy::spdy::kSpdy2Version :
            mod_spdy::spdy::kSpdy3Version);
  }

  net::SpdyFramer framer_;
  net::BufferedSpdyFramer buffered_framer_;
  mod_spdy::SpdyFramePriorityQueue output_queue_;
  MockSpdyServerPushInterface pusher_;
  mod_spdy::LocalPool local_;
  conn_rec* const connection_;
  request_rec* const request_;
  ap_filter_t* const ap_filter_;
  apr_bucket_alloc_t* const bucket_alloc_;
  apr_bucket_brigade* const brigade_;
};

TEST_P(ServerPushFilterTest, SimpleXAssociatedContent) {
  const net::SpdyStreamId stream_id = 3;
  const net::SpdyStreamId associated_stream_id = 0;
  const net::SpdyPriority priority = 1;
  mod_spdy::SpdyStream stream(stream_id, associated_stream_id, priority,
                              net::kSpdyStreamInitialWindowSize,
                              &output_queue_, &buffered_framer_, &pusher_);
  mod_spdy::ServerPushFilter server_push_filter(&stream);

  net::SpdyHeaderBlock headers1;
  headers1[mod_spdy::spdy::kSpdy3Host] = "www.example.com";
  headers1[mod_spdy::spdy::kSpdy3Method] = "GET";
  headers1[mod_spdy::spdy::kSpdy3Path] = "/foo/bar.css?q=12";
  headers1[mod_spdy::spdy::kSpdy3Scheme] = "https";
  headers1[mod_spdy::spdy::kSpdy3Version] = "HTTP/1.1";
  headers1[mod_spdy::http::kReferer] = kRefererUrl;
  EXPECT_CALL(pusher_, StartServerPush(Eq(stream_id), Eq(2u), Eq(headers1)));

  net::SpdyHeaderBlock headers2;
  headers2[mod_spdy::spdy::kSpdy3Host] = "cdn.example.com:8080";
  headers2[mod_spdy::spdy::kSpdy3Method] = "GET";
  headers2[mod_spdy::spdy::kSpdy3Path] = "/images/foo.png";
  headers2[mod_spdy::spdy::kSpdy3Scheme] = "https";
  headers2[mod_spdy::spdy::kSpdy3Version] = "HTTP/1.1";
  headers2[mod_spdy::http::kReferer] = kRefererUrl;
  EXPECT_CALL(pusher_, StartServerPush(Eq(stream_id), Eq(priority),
                                       Eq(headers2)));

  net::SpdyHeaderBlock headers3;
  headers3[mod_spdy::spdy::kSpdy3Host] = "www.example.com";
  headers3[mod_spdy::spdy::kSpdy3Method] = "GET";
  headers3[mod_spdy::spdy::kSpdy3Path] = "/scripts/awesome.js";
  headers3[mod_spdy::spdy::kSpdy3Scheme] = "https";
  headers3[mod_spdy::spdy::kSpdy3Version] = "HTTP/1.1";
  headers3[mod_spdy::http::kReferer] = kRefererUrl;
  EXPECT_CALL(pusher_, StartServerPush(Eq(stream_id), Eq(0u), Eq(headers3)));

  apr_table_setn(request_->headers_out, mod_spdy::http::kXAssociatedContent,
                 "\"https://www.example.com/foo/bar.css?q=12\":2,"
                 "\"https://cdn.example.com:8080/images/foo.png\","
                 "\"/scripts/awesome.js\":0");
  WriteBrigade(&server_push_filter);
  // The X-Associated-Content header should get removed.
  EXPECT_TRUE(apr_table_get(request_->headers_out,
                            mod_spdy::http::kXAssociatedContent) == NULL);
}

TEST_P(ServerPushFilterTest, CopyApplicableHeaders) {
  const net::SpdyStreamId stream_id = 7;
  const net::SpdyStreamId associated_stream_id = 0;
  const net::SpdyPriority priority = 0;
  mod_spdy::SpdyStream stream(stream_id, associated_stream_id, priority,
                              net::kSpdyStreamInitialWindowSize,
                              &output_queue_, &buffered_framer_, &pusher_);
  mod_spdy::ServerPushFilter server_push_filter(&stream);

  // Set some extra headers on the original request (which was evidentally a
  // POST).  The Accept-Language header should get copied over for the push,
  // but the Content-Length header obviously should not.
  apr_table_setn(request_->headers_in, "accept-language", "en-US");
  apr_table_setn(request_->headers_in, "content-length", "200");

  net::SpdyHeaderBlock headers1;
  headers1[mod_spdy::spdy::kSpdy3Host] = "www.example.com";
  headers1[mod_spdy::spdy::kSpdy3Method] = "GET";
  headers1[mod_spdy::spdy::kSpdy3Path] = "/foo/bar.css";
  headers1[mod_spdy::spdy::kSpdy3Scheme] = "https";
  headers1[mod_spdy::spdy::kSpdy3Version] = "HTTP/1.1";
  headers1[mod_spdy::http::kReferer] = kRefererUrl;
  headers1["accept-language"] = "en-US";
  EXPECT_CALL(pusher_, StartServerPush(Eq(stream_id), Eq(2u), Eq(headers1)));

  apr_table_setn(request_->headers_out, mod_spdy::http::kXAssociatedContent,
                 " \"https://www.example.com/foo/bar.css\" : 2 ");
  WriteBrigade(&server_push_filter);
}

TEST_P(ServerPushFilterTest, StopPushingAfterPushError) {
  const net::SpdyStreamId stream_id = 3;
  const net::SpdyStreamId associated_stream_id = 0;
  const net::SpdyPriority priority = 1;
  mod_spdy::SpdyStream stream(stream_id, associated_stream_id, priority,
                              net::kSpdyStreamInitialWindowSize,
                              &output_queue_, &buffered_framer_, &pusher_);
  mod_spdy::ServerPushFilter server_push_filter(&stream);

  // When the filter tries to push the first resource, we reply that pushes are
  // no longer possible on this connection.  The filter should not attempt any
  // more pushes, even though more were specified.
  EXPECT_CALL(pusher_, StartServerPush(Eq(stream_id), Eq(2u), _)).WillOnce(
      Return(mod_spdy::SpdyServerPushInterface::CANNOT_PUSH_EVER_AGAIN));

  apr_table_setn(request_->headers_out, mod_spdy::http::kXAssociatedContent,
                 "\"https://www.example.com/foo/bar.css?q=12\":2,"
                 "\"cdn.example.com:8080/images/foo.png\","
                 "\"/scripts/awesome.js\":0");
  WriteBrigade(&server_push_filter);
  // The X-Associated-Content header should still get removed, though.
  EXPECT_TRUE(apr_table_get(request_->headers_out,
                            mod_spdy::http::kXAssociatedContent) == NULL);
}

TEST_P(ServerPushFilterTest, StopPushingAfterParseError) {
  const net::SpdyStreamId stream_id = 3;
  const net::SpdyStreamId associated_stream_id = 0;
  const net::SpdyPriority priority = 1;
  mod_spdy::SpdyStream stream(stream_id, associated_stream_id, priority,
                              net::kSpdyStreamInitialWindowSize,
                              &output_queue_, &buffered_framer_, &pusher_);
  mod_spdy::ServerPushFilter server_push_filter(&stream);

  // The filter should push the first resource, but then stop when it gets to
  // the parse error.
  EXPECT_CALL(pusher_, StartServerPush(Eq(stream_id), Eq(2u), _));

  apr_table_setn(request_->headers_out, mod_spdy::http::kXAssociatedContent,
                 "\"https://www.example.com/foo/bar.css?q=12\":2,"
                 "oops.iforgot.to/quote/this/url.js,"
                 "\"/scripts/awesome.js\":0");
  WriteBrigade(&server_push_filter);
  // The X-Associated-Content header should still get removed, though.
  EXPECT_TRUE(apr_table_get(request_->headers_out,
                            mod_spdy::http::kXAssociatedContent) == NULL);
}

TEST_P(ServerPushFilterTest, SkipInvalidQuotedUrl) {
  const net::SpdyStreamId stream_id = 3;
  const net::SpdyStreamId associated_stream_id = 0;
  const net::SpdyPriority priority = 1;
  mod_spdy::SpdyStream stream(stream_id, associated_stream_id, priority,
                              net::kSpdyStreamInitialWindowSize,
                              &output_queue_, &buffered_framer_, &pusher_);
  mod_spdy::ServerPushFilter server_push_filter(&stream);

  // The filter should push the first and third resources, but skip the second
  // one because its quoted URL is invalid.
  EXPECT_CALL(pusher_, StartServerPush(Eq(stream_id), Eq(2u), _));
  EXPECT_CALL(pusher_, StartServerPush(Eq(stream_id), Eq(0u), _));

  apr_table_setn(request_->headers_out, mod_spdy::http::kXAssociatedContent,
                 " \"https://www.example.com/foo/bar.css?q=12\" : 2, "
                 "\"https://this.is:not/a valid URL!\":1, "
                 "\"/scripts/awesome.js\":0 ");
  WriteBrigade(&server_push_filter);
  // The X-Associated-Content header should still get removed, though.
  EXPECT_TRUE(apr_table_get(request_->headers_out,
                            mod_spdy::http::kXAssociatedContent) == NULL);
}

TEST_P(ServerPushFilterTest, NoRecursivePushes) {
  const net::SpdyStreamId stream_id = 2;
  const net::SpdyStreamId associated_stream_id = 5;
  const net::SpdyPriority priority = 1;
  mod_spdy::SpdyStream stream(stream_id, associated_stream_id, priority,
                              net::kSpdyStreamInitialWindowSize,
                              &output_queue_, &buffered_framer_, &pusher_);
  mod_spdy::ServerPushFilter server_push_filter(&stream);

  // We should not get any calls to StartServerPush, because we do not allow
  // server-pushed resources to push yet more resources.

  apr_table_setn(request_->headers_out, mod_spdy::http::kXAssociatedContent,
                 "\"https://www.example.com/foo/bar.css?q=12\":2,"
                 "\"cdn.example.com:8080/images/foo.png\","
                 "\"/scripts/awesome.js\":0");
  WriteBrigade(&server_push_filter);
  // The X-Associated-Content header should still get removed, though.
  EXPECT_TRUE(apr_table_get(request_->headers_out,
                            mod_spdy::http::kXAssociatedContent) == NULL);
}

// Run server push tests only over SPDY v3.
INSTANTIATE_TEST_CASE_P(Spdy3, ServerPushFilterTest, testing::Values(3));

// Create a type alias so that we can instantiate some of our
// SpdySessionTest-based tests using a different set of parameters.
typedef ServerPushFilterTest ServerPushFilterSpdy2Test;

TEST_P(ServerPushFilterSpdy2Test, NoPushesForSpdy2) {
  const net::SpdyStreamId stream_id = 3;
  const net::SpdyStreamId associated_stream_id = 0;
  const net::SpdyPriority priority = 1;
  mod_spdy::SpdyStream stream(stream_id, associated_stream_id, priority,
                              net::kSpdyStreamInitialWindowSize,
                              &output_queue_, &buffered_framer_, &pusher_);
  mod_spdy::ServerPushFilter server_push_filter(&stream);

  // We should not get any calls to StartServerPush when we're on SPDY/2.

  apr_table_setn(request_->headers_out, mod_spdy::http::kXAssociatedContent,
                 "\"https://www.example.com/foo/bar.css?q=12\":2,"
                 "\"cdn.example.com:8080/images/foo.png\","
                 "\"/scripts/awesome.js\":0");
  WriteBrigade(&server_push_filter);
  // The X-Associated-Content header should still get removed, though.
  EXPECT_TRUE(apr_table_get(request_->headers_out,
                            mod_spdy::http::kXAssociatedContent) == NULL);
}

INSTANTIATE_TEST_CASE_P(Spdy2, ServerPushFilterSpdy2Test, testing::Values(2));

}  // namespace
