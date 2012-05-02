// Copyright 2010 Google Inc.
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

#include "base/string_util.h"  // for StringToLowerASCII

namespace mod_spdy {

void HeaderPopulatorInterface::MergeInHeader(const base::StringPiece& key,
                                             const base::StringPiece& value,
                                             net::SpdyHeaderBlock* headers) {
  // The SPDY spec requires that header names be lowercase, so forcibly
  // lowercase the key here.
  std::string lower_key(key.as_string());
  StringToLowerASCII(&lower_key);

  net::SpdyHeaderBlock::iterator iter = headers->find(lower_key);
  if (iter == headers->end()) {
    (*headers)[lower_key] = value.as_string();
  } else {
    iter->second.push_back('\0');
    value.AppendToString(&iter->second);
  }
}

}  // namespace mod_spdy
