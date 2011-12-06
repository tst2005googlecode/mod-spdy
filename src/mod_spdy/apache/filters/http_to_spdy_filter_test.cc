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
#include "mod_spdy/common/spdy_frame_priority_queue.h"
#include "mod_spdy/common/spdy_stream.h"
#include "net/spdy/spdy_protocol.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

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

apr_bucket* ImmortalBucket(apr_bucket_alloc_t* bucket_alloc,
                           const base::StringPiece& str) {
  return apr_bucket_immortal_create(str.data(), str.size(), bucket_alloc);
}

TEST(HttpToSpdyFilterTest, Simple) {
  // Set up our data structures that we're testing:
  const spdy::SpdyStreamId stream_id = 3;
  mod_spdy::SpdyFramePriorityQueue output_queue;
  spdy::SpdyFrame* frame;
  mod_spdy::SpdyStream stream(stream_id, SPDY_PRIORITY_HIGHEST, &output_queue);
  mod_spdy::HttpToSpdyFilter http_to_spdy_filter(&stream);

  // Set up some Apache data structures.  To keep things simple, we set only
  // the bare minimum of necessary fields, and rely on apr_pcalloc to zero all
  // others.
  mod_spdy::LocalPool local;
  conn_rec* connection = static_cast<conn_rec*>(
      apr_pcalloc(local.pool(), sizeof(conn_rec)));
  connection->pool = local.pool();
  request_rec* request = static_cast<request_rec*>(
      apr_pcalloc(local.pool(), sizeof(request_rec)));
  request->pool = local.pool();
  request->connection = connection;
  request->protocol = const_cast<char*>("HTTP/1.1");
  request->status = 200;
  request->headers_out = apr_table_make(local.pool(), 3);
  apr_table_setn(request->headers_out, "Connection", "close");
  apr_table_setn(request->headers_out, "Content-Type", "text/html");
  apr_table_setn(request->headers_out, "Host", "www.example.com");
  ap_filter_t* ap_filter = static_cast<ap_filter_t*>(
      apr_pcalloc(local.pool(), sizeof(ap_filter_t)));
  ap_filter->c = connection;
  ap_filter->r = request;
  apr_bucket_alloc_t* bucket_alloc = apr_bucket_alloc_create(local.pool());
  apr_bucket_brigade* brigade = apr_brigade_create(local.pool(), bucket_alloc);

  // Send part of the header data into the filter:
  APR_BRIGADE_INSERT_TAIL(brigade, ImmortalBucket(bucket_alloc, kHeaderData1));
  ASSERT_EQ(APR_SUCCESS, http_to_spdy_filter.Write(ap_filter, brigade));
  apr_brigade_cleanup(brigade);

  // Expect to get a single SYN_STREAM frame out with all the headers (read
  // from request->headers_out rather than the data we put in):
  {
    ASSERT_TRUE(output_queue.Pop(&frame));
    ASSERT_TRUE(frame->is_control_frame());
    const spdy::SpdySynStreamControlFrame& syn_stream_frame =
        *static_cast<spdy::SpdySynStreamControlFrame*>(frame);
    ASSERT_EQ(stream_id, syn_stream_frame.stream_id());
    ASSERT_EQ(spdy::CONTROL_FLAG_NONE, syn_stream_frame.flags());
    // TODO(mdsteele): Once we upgrade our version of SpdyFramer and thus have
    //   an easy way to parse the headers block of a SYN_STREAM frame without
    //   decompression, check the key/value pairs here and make sure we get out
    //   the same headers we put in.
    delete frame;
  }
  ASSERT_FALSE(output_queue.Pop(&frame));

  // Send the rest of the header data into the filter:
  APR_BRIGADE_INSERT_TAIL(brigade, ImmortalBucket(bucket_alloc, kHeaderData2));
  ASSERT_EQ(APR_SUCCESS, http_to_spdy_filter.Write(ap_filter, brigade));
  apr_brigade_cleanup(brigade);

  // Expect to get nothing more out yet:
  ASSERT_FALSE(output_queue.Pop(&frame));

  // Now send in some body data (kBodyData1), with a FLUSH bucket:
  request->sent_bodyct = 1;
  APR_BRIGADE_INSERT_TAIL(brigade, ImmortalBucket(bucket_alloc, kBodyData1));
  APR_BRIGADE_INSERT_TAIL(brigade, apr_bucket_flush_create(bucket_alloc));
  ASSERT_EQ(APR_SUCCESS, http_to_spdy_filter.Write(ap_filter, brigade));
  apr_brigade_cleanup(brigade);

  // Expect to get a single data frame out, containing kBodyData1:
  {
    ASSERT_TRUE(output_queue.Pop(&frame));
    ASSERT_FALSE(frame->is_control_frame());
    const spdy::SpdyDataFrame& data_frame =
        *static_cast<spdy::SpdyDataFrame*>(frame);
    ASSERT_EQ(stream_id, data_frame.stream_id());
    ASSERT_EQ(spdy::DATA_FLAG_NONE, data_frame.flags());
    ASSERT_EQ(kBodyData1, std::string(data_frame.payload(),
                                      data_frame.length()));
    delete frame;
  }
  ASSERT_FALSE(output_queue.Pop(&frame));

  // Send in some more body data (kBodyData2), this time with no FLUSH bucket:
  APR_BRIGADE_INSERT_TAIL(brigade, ImmortalBucket(bucket_alloc, kBodyData2));
  ASSERT_EQ(APR_SUCCESS, http_to_spdy_filter.Write(ap_filter, brigade));
  apr_brigade_cleanup(brigade);

  // Expect to get nothing more out yet (because there's too little data to be
  // worth sending a frame):
  ASSERT_FALSE(output_queue.Pop(&frame));

  // Send lots more body data, again with a FLUSH bucket:
  {
    const std::string big_data(6000, 'x');
    APR_BRIGADE_INSERT_TAIL(brigade, ImmortalBucket(bucket_alloc, big_data));
    APR_BRIGADE_INSERT_TAIL(brigade, apr_bucket_flush_create(bucket_alloc));
    ASSERT_EQ(APR_SUCCESS, http_to_spdy_filter.Write(ap_filter, brigade));
    apr_brigade_cleanup(brigade);
  }

  // This time, we should get two data frames out, the first of which should
  // start with kBodyData2:
  {
    ASSERT_TRUE(output_queue.Pop(&frame));
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
    ASSERT_TRUE(output_queue.Pop(&frame));
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
  ASSERT_FALSE(output_queue.Pop(&frame));

  // Finally, send a bunch more data ending with kBodyData3, followed by an EOS
  // bucket:
  {
    const std::string big_data(6000, 'y');
    APR_BRIGADE_INSERT_TAIL(brigade, ImmortalBucket(bucket_alloc, big_data));
    APR_BRIGADE_INSERT_TAIL(brigade, ImmortalBucket(bucket_alloc, kBodyData3));
    APR_BRIGADE_INSERT_TAIL(brigade, apr_bucket_eos_create(bucket_alloc));
    ASSERT_EQ(APR_SUCCESS, http_to_spdy_filter.Write(ap_filter, brigade));
    apr_brigade_cleanup(brigade);
  }

  // We should get two last data frames, the latter ending with kBodyData3 and
  // having FLAG_FIN set.
  {
    ASSERT_TRUE(output_queue.Pop(&frame));
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
    ASSERT_TRUE(output_queue.Pop(&frame));
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
  ASSERT_FALSE(output_queue.Pop(&frame));
}

}  // namespace
