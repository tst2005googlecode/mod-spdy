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

#include "mod_spdy/common/testing/spdy_frame_matchers.h"

#include <iostream>
#include <string>

#include "base/stringprintf.h"
#include "net/spdy/spdy_protocol.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace {

std::string ControlTypeName(net::SpdyControlType type) {
  switch (type) {
    case net::SYN_STREAM:    return "SYN_STREAM";
    case net::SYN_REPLY:     return "SYN_REPLY";
    case net::RST_STREAM:    return "RST_STREAM";
    case net::SETTINGS:      return "SETTINGS";
    case net::NOOP:          return "NOOP";
    case net::PING:          return "PING";
    case net::GOAWAY:        return "GOAWAY";
    case net::HEADERS:       return "HEADERS";
    case net::WINDOW_UPDATE: return "WINDOW_UPDATE";
    default: return base::StringPrintf("UNKNOWN(%d)", type);
  }
}

}  // namespace

namespace mod_spdy {

bool IsControlFrameOfTypeMatcher::MatchAndExplain(
    const net::SpdyFrame& frame,
    testing::MatchResultListener* listener) const {
  if (!frame.is_control_frame()) {
    *listener << "is a data frame";
    return false;
  }
  const net::SpdyControlFrame* ctrl_frame =
      static_cast<const net::SpdyControlFrame*>(&frame);
  if (ctrl_frame->type() != type_) {
    *listener << "is a " << ControlTypeName(ctrl_frame->type()) << " frame";
    return false;
  }
  return true;
}

void IsControlFrameOfTypeMatcher::DescribeTo(std::ostream* out) const {
  *out << "is a " << ControlTypeName(type_) << " frame";
}

void IsControlFrameOfTypeMatcher::DescribeNegationTo(std::ostream* out) const {
  *out << "isn't a " << ControlTypeName(type_) << " frame";
}

bool IsDataFrameMatcher::MatchAndExplain(
    const net::SpdyFrame& frame,
    testing::MatchResultListener* listener) const {
  if (frame.is_control_frame()) {
    *listener << "is a " << ControlTypeName(
        static_cast<const net::SpdyControlFrame*>(&frame)->type())
              << " frame";
    return false;
  }
  return true;
}

void IsDataFrameMatcher::DescribeTo(std::ostream* out) const {
  *out << "is a data frame";
}

void IsDataFrameMatcher::DescribeNegationTo(std::ostream* out) const {
  *out << "isn't a data frame";
}

bool FlagFinIsMatcher::MatchAndExplain(
    const net::SpdyFrame& frame,
    testing::MatchResultListener* listener) const {
  const bool fin = frame.is_control_frame() ?
      (frame.flags() & net::CONTROL_FLAG_FIN) :
      (frame.flags() & net::DATA_FLAG_FIN);
  if (fin != fin_) {
    *listener << (fin ? "has FLAG_FIN set" : "doesn't have FLAG_FIN set");
    return false;
  }
  return true;
}

void FlagFinIsMatcher::DescribeTo(std::ostream* out) const {
  *out << (fin_ ? "has FLAG_FIN set" : "doesn't have FLAG_FIN set");
}

void FlagFinIsMatcher::DescribeNegationTo(std::ostream* out) const {
  *out << (fin_ ? "doesn't have FLAG_FIN set" : "has FLAG_FIN set");
}

}  // namespace mod_spdy
