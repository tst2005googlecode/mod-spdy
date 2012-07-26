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

#include "mod_spdy/apache/master_connection_context.h"

#include "base/logging.h"
#include "mod_spdy/common/spdy_stream.h"

namespace mod_spdy {

MasterConnectionContext::MasterConnectionContext(bool using_ssl)
    : using_ssl_(using_ssl),
      npn_state_(NOT_DONE_YET),
      assume_spdy_(false),
      spdy_version_(0) {}

MasterConnectionContext::~MasterConnectionContext() {}

bool MasterConnectionContext::is_using_spdy() const {
  const bool using_spdy = (npn_state_ == USING_SPDY || assume_spdy_);
  return using_spdy;
}

MasterConnectionContext::NpnState MasterConnectionContext::npn_state() const {
  return npn_state_;
}

void MasterConnectionContext::set_npn_state(NpnState state) {
  npn_state_ = state;
}

bool MasterConnectionContext::is_assuming_spdy() const {
  return assume_spdy_;
}

void MasterConnectionContext::set_assume_spdy(bool assume) {
  assume_spdy_ = assume;
}

int MasterConnectionContext::spdy_version() const {
  DCHECK(is_using_spdy());
  DCHECK_GT(spdy_version_, 0);
  return spdy_version_;
}

void MasterConnectionContext::set_spdy_version(int spdy_version) {
  DCHECK(is_using_spdy());
  DCHECK_EQ(spdy_version_, 0);
  DCHECK_GT(spdy_version, 0);
  spdy_version_ = spdy_version;
}

}  // namespace mod_spdy
