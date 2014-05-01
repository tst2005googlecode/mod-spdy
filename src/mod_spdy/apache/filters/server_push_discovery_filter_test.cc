// Copyright 2014 Google Inc.
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

#include "mod_spdy/apache/filters/server_push_discovery_filter.h"

#include <string>

#include "httpd.h"
#include "apr_buckets.h"
#include "apr_strings.h"
#include "apr_tables.h"
#include "util_filter.h"

#include "base/strings/string_piece.h"
#include "base/strings/stringprintf.h"
#include "mod_spdy/apache/pool_util.h"
#include "mod_spdy/common/protocol_util.h"
#include "mod_spdy/common/server_push_discovery_learner.h"
#include "mod_spdy/common/server_push_discovery_session.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

const char* const kRefererUrl = "https://www.example.com/index.html";

class ServerPushDiscoveryFilterTest : public testing::Test {
 public:
  ServerPushDiscoveryFilterTest()
      : bucket_alloc_(apr_bucket_alloc_create(local_.pool())),
        brigade_(apr_brigade_create(local_.pool(), bucket_alloc_)) {
  }

  virtual void SetUp() {
    RunThroughFilter("index.html", 0, -1, mod_spdy::spdy::SPDY_VERSION_NONE);
    RunThroughFilter("a.html", 10, 0, mod_spdy::spdy::SPDY_VERSION_NONE);
    RunThroughFilter("b.html", 20, 0, mod_spdy::spdy::SPDY_VERSION_NONE);
  }

  ap_filter_t* RunThroughFilter(
      const std::string& uri, apr_time_t request_time, int session_id,
      mod_spdy::spdy::SpdyVersion spdy_version) {
    ap_filter_t* ap_filter = MakeFilterChain(uri, request_time, session_id);
    mod_spdy::ServerPushDiscoveryFilter(ap_filter, brigade_, &learner_,
                                        &session_pool_, spdy_version, true);
    return ap_filter;
  }

  // No session cookie is written when |session_id| is negative.
  ap_filter_t* MakeFilterChain(
      const std::string& uri, apr_time_t request_time, int session_id) {
    conn_rec* const connection(static_cast<conn_rec*>(
        apr_pcalloc(local_.pool(), sizeof(conn_rec))));
    request_rec* const request(static_cast<request_rec*>(
        apr_pcalloc(local_.pool(), sizeof(request_rec))));
    ap_filter_t* const ap_filter(static_cast<ap_filter_t*>(
        apr_pcalloc(local_.pool(), sizeof(ap_filter_t))));

    // Set up our Apache data structures.  To keep things simple, we set only
    // the bare minimum of necessary fields, and rely on apr_pcalloc to zero
    // all others.
    connection->pool = local_.pool();
    request->pool = local_.pool();
    request->connection = connection;
    request->request_time = request_time;
    request->headers_in = apr_table_make(local_.pool(), 5);
    request->headers_out = apr_table_make(local_.pool(), 5);
    request->err_headers_out = apr_table_make(local_.pool(), 5);
    request->protocol = const_cast<char*>("HTTP/1.1");
    request->uri = apr_pstrdup(local_.pool(), uri.c_str());
    ap_filter->c = connection;
    ap_filter->r = request;

    if (session_id >= 0) {
      std::string header_value =
          base::StringPrintf("%s=%d", "ServerPushDiscoverySession", session_id);
      apr_table_set(request->headers_in, "Cookie", header_value.c_str());
    }

    apr_table_setn(request->headers_in, mod_spdy::http::kHost,
                   "www.example.com");

    return ap_filter;
  }

  std::string GetXACHeader(ap_filter_t* ap_filter) {
    const char* value = apr_table_get(ap_filter->r->headers_out,
                                      mod_spdy::http::kXAssociatedContent);
    if (value) {
      return std::string(value);
    } else {
      return std::string();
    }
  }

 protected:
  mod_spdy::LocalPool local_;
  mod_spdy::ServerPushDiscoveryLearner learner_;
  mod_spdy::ServerPushDiscoverySessionPool session_pool_;
  apr_bucket_alloc_t* const bucket_alloc_;
  apr_bucket_brigade* const brigade_;
};

TEST_F(ServerPushDiscoveryFilterTest, UntrainedNoEffect) {
  ap_filter_t* const ap_filter(RunThroughFilter(
      "untrained.html", 0, -1, mod_spdy::spdy::SPDY_VERSION_3_1));
  EXPECT_EQ("", GetXACHeader(ap_filter));
}

TEST_F(ServerPushDiscoveryFilterTest, GenerateHeadersForSPDY3) {
  ap_filter_t* const ap_filter(RunThroughFilter(
      "index.html", 50, -1, mod_spdy::spdy::SPDY_VERSION_3_1));
  EXPECT_EQ("\"a.html\":2,\"b.html\":5,", GetXACHeader(ap_filter));
}

TEST_F(ServerPushDiscoveryFilterTest, NoHeadersForSPDY2) {
  ap_filter_t* const ap_filter(RunThroughFilter(
      "index.html", 50, -1, mod_spdy::spdy::SPDY_VERSION_2));
  EXPECT_EQ("", GetXACHeader(ap_filter));
}

TEST_F(ServerPushDiscoveryFilterTest, NoHeadersForHTTPS) {
  ap_filter_t* const ap_filter(RunThroughFilter(
      "index.html", 50, -1, mod_spdy::spdy::SPDY_VERSION_NONE));
  EXPECT_EQ("", GetXACHeader(ap_filter));
}

TEST_F(ServerPushDiscoveryFilterTest, PassThroughExistingXACHeaders) {
  ap_filter_t* const ap_filter(MakeFilterChain("index.html", 80, -1));
  apr_table_add(ap_filter->r->headers_out,
                mod_spdy::http::kXAssociatedContent, "TestXACValue");
  mod_spdy::ServerPushDiscoveryFilter(ap_filter, brigade_,
                                      &learner_, &session_pool_,
                                      mod_spdy::spdy::SPDY_VERSION_3_1, true);
  EXPECT_EQ("TestXACValue", GetXACHeader(ap_filter));
}

TEST_F(ServerPushDiscoveryFilterTest, AdditionalTraining) {
  RunThroughFilter("index.html", 100, -1, mod_spdy::spdy::SPDY_VERSION_NONE);
  RunThroughFilter("b.html", 110, 1, mod_spdy::spdy::SPDY_VERSION_NONE);
  RunThroughFilter("c.html", 120, 1, mod_spdy::spdy::SPDY_VERSION_NONE);

  RunThroughFilter("index.html", 200, -1, mod_spdy::spdy::SPDY_VERSION_NONE);
  RunThroughFilter("b.html", 210, 2, mod_spdy::spdy::SPDY_VERSION_NONE);
  RunThroughFilter("c.html", 220, 2, mod_spdy::spdy::SPDY_VERSION_NONE);

  RunThroughFilter("index.html", 300, -1, mod_spdy::spdy::SPDY_VERSION_NONE);
  RunThroughFilter("b.html", 310, 2, mod_spdy::spdy::SPDY_VERSION_NONE);
  RunThroughFilter("c.html", 320, 2, mod_spdy::spdy::SPDY_VERSION_NONE);

  ap_filter_t* const ap_filter(RunThroughFilter(
      "index.html", 350, -1, mod_spdy::spdy::SPDY_VERSION_3_1));
  EXPECT_EQ("\"b.html\":2,\"c.html\":5,", GetXACHeader(ap_filter));
}

}  // namespace
