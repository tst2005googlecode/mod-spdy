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

#include "mod_spdy/apache/filters/http_to_spdy_filter.h"

#include "httpd.h"
#include "apr_buckets.h"
#include "apr_tables.h"
#include "util_filter.h"

#include "base/string_piece.h"
#include "mod_spdy/apache/pool_util.h"
#include "mod_spdy/common/protocol_util.h"
#include "mod_spdy/common/spdy_frame_priority_queue.h"
#include "mod_spdy/common/spdy_stream.h"
#include "mod_spdy/common/version.h"
#include "net/spdy/spdy_frame_builder.h"
#include "net/spdy/spdy_protocol.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

class HttpToSpdyFilterTest : public testing::Test {
 public:
  HttpToSpdyFilterTest()
      : connection_(static_cast<conn_rec*>(
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
    request_->protocol = const_cast<char*>("HTTP/1.1");
    request_->status = 200;
    request_->headers_out = apr_table_make(local_.pool(), 3);
    ap_filter_->c = connection_;
    ap_filter_->r = request_;
  }

 protected:
  void AddImmortalBucket(const base::StringPiece& str) {
    APR_BRIGADE_INSERT_TAIL(brigade_, apr_bucket_immortal_create(
        str.data(), str.size(), bucket_alloc_));
  }

  void AddFlushBucket() {
    APR_BRIGADE_INSERT_TAIL(brigade_, apr_bucket_flush_create(bucket_alloc_));
  }

  void AddEosBucket() {
    APR_BRIGADE_INSERT_TAIL(brigade_, apr_bucket_eos_create(bucket_alloc_));
  }

  apr_status_t WriteBrigade(mod_spdy::HttpToSpdyFilter* filter) {
    apr_status_t status = filter->Write(ap_filter_, brigade_);
    if (!APR_BRIGADE_EMPTY(brigade_)) {
      ADD_FAILURE() << "Brigade not empty after filter->Write()";
      apr_brigade_cleanup(brigade_);
    }
    return status;
  }

  mod_spdy::SpdyFramePriorityQueue output_queue_;
  mod_spdy::LocalPool local_;
  conn_rec* const connection_;
  request_rec* const request_;
  ap_filter_t* const ap_filter_;
  apr_bucket_alloc_t* const bucket_alloc_;
  apr_bucket_brigade* const brigade_;
};

const char* kHeaderData1 =
    "GET /foo/bar/index.html HTTP/1.1\r\n"
    "Connection: close\r\n";
const char* kHeaderData2 =
    "Content-Type: text/html\r\n"
    "Host: www.example.com\r\n"
    "\r\n";
const char* kBodyData1 =
    "<html>\n"
    "  <head><title>Hello, world!</title></head>\n"
    "  <body>\n";
const char* kBodyData2 =
    "    <p>Hello, world!</p>\n"
    "    <p>Foo bar baz.</p>\n";
const char* kBodyData3 =
    "  </body>\n"
    "</html>\n";

TEST_F(HttpToSpdyFilterTest, ClientRequest) {
  // Set up our data structures that we're testing:
  const spdy::SpdyStreamId stream_id = 3;
  const spdy::SpdyStreamId associated_stream_id = 0;
  const spdy::SpdyPriority priority = SPDY_PRIORITY_HIGHEST;
  mod_spdy::SpdyStream stream(stream_id, associated_stream_id, priority,
                              &output_queue_);
  mod_spdy::HttpToSpdyFilter http_to_spdy_filter(&stream);
  spdy::SpdyFrame* frame;

  // Set the response headers of the request:
  apr_table_setn(request_->headers_out, "Connection", "close");
  apr_table_setn(request_->headers_out, "Content-Type", "text/html");
  apr_table_setn(request_->headers_out, "Host", "www.example.com");

  // Send part of the header data into the filter:
  AddImmortalBucket(kHeaderData1);
  ASSERT_EQ(APR_SUCCESS, WriteBrigade(&http_to_spdy_filter));

  // Expect to get a single SYN_REPLY frame out with all the headers (read
  // from request->headers_out rather than the data we put in):
  {
    ASSERT_TRUE(output_queue_.Pop(&frame));
    ASSERT_TRUE(frame != NULL);
    ASSERT_TRUE(frame->is_control_frame());
    ASSERT_EQ(spdy::SYN_REPLY,
              static_cast<spdy::SpdyControlFrame*>(frame)->type());
    const spdy::SpdySynReplyControlFrame& syn_reply_frame =
        *static_cast<spdy::SpdySynReplyControlFrame*>(frame);
    ASSERT_EQ(stream_id, syn_reply_frame.stream_id());
    ASSERT_EQ(spdy::CONTROL_FLAG_NONE, syn_reply_frame.flags());

    spdy::SpdyHeaderBlock block;
    ASSERT_TRUE(mod_spdy::ParseHeaderBlockInBuffer(
        syn_reply_frame.header_block(),
        syn_reply_frame.header_block_len(),
        &block));
    EXPECT_EQ(5, block.size());
    EXPECT_EQ("text/html", block[mod_spdy::http::kContentType]);
    EXPECT_EQ("www.example.com", block[mod_spdy::http::kHost]);
    EXPECT_EQ("200", block[spdy::kStatus]);
    EXPECT_EQ("HTTP/1.1", block[spdy::kVersion]);
    EXPECT_EQ(MOD_SPDY_VERSION_STRING "-" LASTCHANGE_STRING,
              block[mod_spdy::http::kXModSpdy]);
    delete frame;
  }
  ASSERT_FALSE(output_queue_.Pop(&frame));

  // Send the rest of the header data into the filter:
  AddImmortalBucket(kHeaderData2);
  ASSERT_EQ(APR_SUCCESS, WriteBrigade(&http_to_spdy_filter));

  // Expect to get nothing more out yet:
  ASSERT_FALSE(output_queue_.Pop(&frame));

  // Now send in some body data (kBodyData1), with a FLUSH bucket:
  request_->sent_bodyct = 1;
  AddImmortalBucket(kBodyData1);
  AddFlushBucket();
  ASSERT_EQ(APR_SUCCESS, WriteBrigade(&http_to_spdy_filter));

  // Expect to get a single data frame out, containing kBodyData1:
  {
    ASSERT_TRUE(output_queue_.Pop(&frame));
    ASSERT_FALSE(frame->is_control_frame());
    const spdy::SpdyDataFrame& data_frame =
        *static_cast<spdy::SpdyDataFrame*>(frame);
    ASSERT_EQ(stream_id, data_frame.stream_id());
    ASSERT_EQ(spdy::DATA_FLAG_NONE, data_frame.flags());
    ASSERT_EQ(kBodyData1, std::string(data_frame.payload(),
                                      data_frame.length()));
    delete frame;
  }
  ASSERT_FALSE(output_queue_.Pop(&frame));

  // Send in some more body data (kBodyData2), this time with no FLUSH bucket:
  AddImmortalBucket(kBodyData2);
  ASSERT_EQ(APR_SUCCESS, WriteBrigade(&http_to_spdy_filter));

  // Expect to get nothing more out yet (because there's too little data to be
  // worth sending a frame):
  ASSERT_FALSE(output_queue_.Pop(&frame));

  // Send lots more body data, again with a FLUSH bucket:
  {
    const std::string big_data(6000, 'x');
    AddImmortalBucket(big_data);
    AddFlushBucket();
    ASSERT_EQ(APR_SUCCESS, WriteBrigade(&http_to_spdy_filter));
  }

  // This time, we should get two data frames out, the first of which should
  // start with kBodyData2:
  {
    ASSERT_TRUE(output_queue_.Pop(&frame));
    ASSERT_FALSE(frame->is_control_frame());
    const spdy::SpdyDataFrame& data_frame =
        *static_cast<spdy::SpdyDataFrame*>(frame);
    ASSERT_EQ(stream_id, data_frame.stream_id());
    ASSERT_EQ(spdy::DATA_FLAG_NONE, data_frame.flags());
    const base::StringPiece data(data_frame.payload(), data_frame.length());
    ASSERT_TRUE(data.starts_with(kBodyData2));
    ASSERT_TRUE(data.ends_with("xxxxxxx"));
    delete frame;
  }
  {
    ASSERT_TRUE(output_queue_.Pop(&frame));
    ASSERT_FALSE(frame->is_control_frame());
    const spdy::SpdyDataFrame& data_frame =
        *static_cast<spdy::SpdyDataFrame*>(frame);
    ASSERT_EQ(stream_id, data_frame.stream_id());
    ASSERT_EQ(spdy::DATA_FLAG_NONE, data_frame.flags());
    const base::StringPiece data(data_frame.payload(), data_frame.length());
    ASSERT_TRUE(data.starts_with("xxxxxxx"));
    ASSERT_TRUE(data.ends_with("xxxxxxx"));
    delete frame;
  }
  ASSERT_FALSE(output_queue_.Pop(&frame));

  // Finally, send a bunch more data ending with kBodyData3, followed by an EOS
  // bucket:
  {
    const std::string big_data(6000, 'y');
    AddImmortalBucket(big_data);
    AddImmortalBucket(kBodyData3);
    AddEosBucket();
    ASSERT_EQ(APR_SUCCESS, WriteBrigade(&http_to_spdy_filter));
  }

  // We should get two last data frames, the latter ending with kBodyData3 and
  // having FLAG_FIN set.
  {
    ASSERT_TRUE(output_queue_.Pop(&frame));
    ASSERT_FALSE(frame->is_control_frame());
    const spdy::SpdyDataFrame& data_frame =
        *static_cast<spdy::SpdyDataFrame*>(frame);
    ASSERT_EQ(stream_id, data_frame.stream_id());
    ASSERT_EQ(spdy::DATA_FLAG_NONE, data_frame.flags());
    const base::StringPiece data(data_frame.payload(), data_frame.length());
    ASSERT_TRUE(data.starts_with("yyyyyyy"));
    ASSERT_TRUE(data.ends_with("yyyyyyy"));
    delete frame;
  }
  {
    ASSERT_TRUE(output_queue_.Pop(&frame));
    ASSERT_FALSE(frame->is_control_frame());
    const spdy::SpdyDataFrame& data_frame =
        *static_cast<spdy::SpdyDataFrame*>(frame);
    ASSERT_EQ(stream_id, data_frame.stream_id());
    ASSERT_EQ(spdy::DATA_FLAG_FIN, data_frame.flags());
    const base::StringPiece data(data_frame.payload(), data_frame.length());
    ASSERT_TRUE(data.starts_with("yyyyyyy"));
    ASSERT_TRUE(data.ends_with(kBodyData3));
    delete frame;
  }
  ASSERT_FALSE(output_queue_.Pop(&frame));
}

const char* kServerPushHeaderData =
    "GET /foo/bar/style.css HTTP/1.1\r\n"
    "Connection: close\r\n"
    "Content-Type: text/css\r\n"
    "Host: www.example.com\r\n"
    "\r\n";
const char* kServerPushBodyData =
    "BODY { color: red; }\n"
    "H1 { color: blue; }\n";

TEST_F(HttpToSpdyFilterTest, ServerPush) {
  // Set up our data structures that we're testing:
  const spdy::SpdyStreamId stream_id = 4;
  const spdy::SpdyStreamId associated_stream_id = 3;
  const spdy::SpdyPriority priority = 1;
  mod_spdy::SpdyStream stream(stream_id, associated_stream_id, priority,
                              &output_queue_);
  mod_spdy::HttpToSpdyFilter http_to_spdy_filter(&stream);
  spdy::SpdyFrame* frame;

  // Set the response headers of the request:
  apr_table_setn(request_->headers_out, "Connection", "close");
  apr_table_setn(request_->headers_out, "Content-Type", "text/css");
  apr_table_setn(request_->headers_out, "Host", "www.example.com");

  // Send the header data into the filter:
  AddImmortalBucket(kServerPushHeaderData);
  ASSERT_EQ(APR_SUCCESS, WriteBrigade(&http_to_spdy_filter));

  // This is a server push, so expect to get a single SYN_STREAM frame (rather
  // than a SYN_REPLY frame) with all the headers and with FLAG_UNIDIRECTIONAL
  // set:
  {
    ASSERT_TRUE(output_queue_.Pop(&frame));
    ASSERT_TRUE(frame->is_control_frame());
    ASSERT_EQ(spdy::SYN_STREAM,
              static_cast<spdy::SpdyControlFrame*>(frame)->type());
    const spdy::SpdySynStreamControlFrame& syn_stream_frame =
        *static_cast<spdy::SpdySynStreamControlFrame*>(frame);
    ASSERT_EQ(stream_id, syn_stream_frame.stream_id());
    ASSERT_EQ(associated_stream_id, syn_stream_frame.associated_stream_id());
    ASSERT_EQ(priority, syn_stream_frame.priority());
    ASSERT_EQ(spdy::CONTROL_FLAG_UNIDIRECTIONAL, syn_stream_frame.flags());

    spdy::SpdyHeaderBlock block;
    ASSERT_TRUE(mod_spdy::ParseHeaderBlockInBuffer(
        syn_stream_frame.header_block(),
        syn_stream_frame.header_block_len(),
        &block));
    EXPECT_EQ(5, block.size());
    EXPECT_EQ("text/css", block[mod_spdy::http::kContentType]);
    EXPECT_EQ("www.example.com", block[mod_spdy::http::kHost]);
    EXPECT_EQ("200", block[spdy::kStatus]);
    EXPECT_EQ("HTTP/1.1", block[spdy::kVersion]);
    EXPECT_EQ(MOD_SPDY_VERSION_STRING "-" LASTCHANGE_STRING,
              block[mod_spdy::http::kXModSpdy]);
    delete frame;
  }
  ASSERT_FALSE(output_queue_.Pop(&frame));

  // Now send in the body data followed by an EOS bucket:
  request_->sent_bodyct = 1;
  AddImmortalBucket(kServerPushBodyData);
  AddEosBucket();
  ASSERT_EQ(APR_SUCCESS, WriteBrigade(&http_to_spdy_filter));

  // We should get a single data frame, with FLAG_FIN set.
  {
    ASSERT_TRUE(output_queue_.Pop(&frame));
    ASSERT_FALSE(frame->is_control_frame());
    const spdy::SpdyDataFrame& data_frame =
        *static_cast<spdy::SpdyDataFrame*>(frame);
    ASSERT_EQ(stream_id, data_frame.stream_id());
    ASSERT_EQ(spdy::DATA_FLAG_FIN, data_frame.flags());
    const base::StringPiece data(data_frame.payload(), data_frame.length());
    ASSERT_EQ(kServerPushBodyData, data);
    delete frame;
  }
  ASSERT_FALSE(output_queue_.Pop(&frame));
}

}  // namespace
