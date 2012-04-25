// Copyright 2012 Google Inc. All Rights Reserved.
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

#ifndef MOD_SPDY_TESTING_SPDY_FRAME_MATCHERS_H_
#define MOD_SPDY_TESTING_SPDY_FRAME_MATCHERS_H_

#include <iostream>

#include "base/basictypes.h"
#include "net/spdy/spdy_protocol.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace mod_spdy {

class IsControlFrameOfTypeMatcher :
      public testing::MatcherInterface<const net::SpdyFrame&> {
 public:
  IsControlFrameOfTypeMatcher(net::SpdyControlType type) : type_(type) {}
  virtual ~IsControlFrameOfTypeMatcher() {}
  virtual bool MatchAndExplain(const net::SpdyFrame& frame,
                               testing::MatchResultListener* listener) const;
  virtual void DescribeTo(std::ostream* out) const;
  virtual void DescribeNegationTo(std::ostream* out) const;
 private:
  const net::SpdyControlType type_;
  DISALLOW_COPY_AND_ASSIGN(IsControlFrameOfTypeMatcher);
};

// Make a matcher that requires the argument to be a control frame of the given
// type.
inline testing::Matcher<const net::SpdyFrame&> IsControlFrameOfType(
    net::SpdyControlType type) {
  return testing::MakeMatcher(new IsControlFrameOfTypeMatcher(type));
}

class IsDataFrameMatcher :
      public testing::MatcherInterface<const net::SpdyFrame&> {
 public:
  IsDataFrameMatcher() {}
  virtual ~IsDataFrameMatcher() {}
  virtual bool MatchAndExplain(const net::SpdyFrame& frame,
                               testing::MatchResultListener* listener) const;
  virtual void DescribeTo(std::ostream* out) const;
  virtual void DescribeNegationTo(std::ostream* out) const;
 private:
  DISALLOW_COPY_AND_ASSIGN(IsDataFrameMatcher);
};

// Make a matcher that requires the argument to be a DATA frame.
inline testing::Matcher<const net::SpdyFrame&> IsDataFrame() {
  return testing::MakeMatcher(new IsDataFrameMatcher);
}

class FlagFinIsMatcher :
      public testing::MatcherInterface<const net::SpdyFrame&> {
 public:
  FlagFinIsMatcher(bool fin) : fin_(fin) {}
  virtual ~FlagFinIsMatcher() {}
  virtual bool MatchAndExplain(const net::SpdyFrame& frame,
                               testing::MatchResultListener* listener) const;
  virtual void DescribeTo(std::ostream* out) const;
  virtual void DescribeNegationTo(std::ostream* out) const;
 private:
  const bool fin_;
  DISALLOW_COPY_AND_ASSIGN(FlagFinIsMatcher);
};

// Make a matcher that requires the frame to have the given FLAG_FIN value.
inline testing::Matcher<const net::SpdyFrame&> FlagFinIs(bool fin) {
  return testing::MakeMatcher(new FlagFinIsMatcher(fin));
}

}  // namespace mod_spdy

#endif  // MOD_SPDY_TESTING_SPDY_FRAME_MATCHERS_H_
