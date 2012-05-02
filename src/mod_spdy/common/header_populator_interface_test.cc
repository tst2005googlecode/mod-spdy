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

#include "mod_spdy/common/header_populator_interface.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

using mod_spdy::HeaderPopulatorInterface;

TEST(HeaderPopulatorInterfaceTest, MergeIntoEmpty) {
  net::SpdyHeaderBlock headers;
  ASSERT_EQ(0u, headers.size());

  HeaderPopulatorInterface::MergeInHeader("content-length", "256", &headers);
  ASSERT_EQ(1u, headers.size());
  ASSERT_EQ("256", headers["content-length"]);
}

TEST(HeaderPopulatorInterfaceTest, MakeLowerCase) {
  net::SpdyHeaderBlock headers;
  ASSERT_EQ(0u, headers.size());

  HeaderPopulatorInterface::MergeInHeader("Content-Length", "256", &headers);
  ASSERT_EQ(1u, headers.size());
  ASSERT_EQ(0u, headers.count("Content-Length"));
  ASSERT_EQ("256", headers["content-length"]);
}

TEST(HeaderPopulatorInterfaceTest, MergeDifferentHeaders) {
  net::SpdyHeaderBlock headers;
  ASSERT_EQ(0u, headers.size());

  HeaderPopulatorInterface::MergeInHeader("x-foo", "bar", &headers);
  ASSERT_EQ(1u, headers.size());
  ASSERT_EQ("bar", headers["x-foo"]);

  HeaderPopulatorInterface::MergeInHeader("x-baz", "quux", &headers);
  ASSERT_EQ(2u, headers.size());
  ASSERT_EQ("bar", headers["x-foo"]);
  ASSERT_EQ("quux", headers["x-baz"]);
}

TEST(HeaderPopulatorInterfaceTest, MergeRepeatedHeader) {
  net::SpdyHeaderBlock headers;
  ASSERT_EQ(0u, headers.size());

  HeaderPopulatorInterface::MergeInHeader("x-foo", "bar", &headers);
  ASSERT_EQ(1u, headers.size());
  const std::string expected1("bar");
  ASSERT_EQ(expected1, headers["x-foo"]);

  HeaderPopulatorInterface::MergeInHeader("x-foo", "baz", &headers);
  ASSERT_EQ(1u, headers.size());
  const std::string expected2("bar\0baz", 7);
  ASSERT_EQ(expected2, headers["x-foo"]);

  HeaderPopulatorInterface::MergeInHeader("x-foo", "quux", &headers);
  ASSERT_EQ(1u, headers.size());
  const std::string expected3("bar\0baz\0quux", 12);
  ASSERT_EQ(expected3, headers["x-foo"]);
}

}  // namespace
