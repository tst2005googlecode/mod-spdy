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

#include "mod_spdy/common/connection_context.h"

#include "base/logging.h"

namespace mod_spdy {

ConnectionContext::ConnectionContext()
    : npn_state_(NOT_DONE_YET),
      assume_spdy_(false),
      slave_stream_(NULL) {}

ConnectionContext::ConnectionContext(SpdyStream* slave_stream)
    : npn_state_(USING_SPDY),
      assume_spdy_(false),
      slave_stream_(slave_stream) {}

ConnectionContext::~ConnectionContext() {}

bool ConnectionContext::is_using_spdy() const {
  const bool using_spdy = (npn_state_ == USING_SPDY || assume_spdy_);
  DCHECK(using_spdy || !is_slave());
  return using_spdy;
}

SpdyStream* ConnectionContext::slave_stream() const {
  DCHECK(is_slave());
  DCHECK(slave_stream_ != NULL);
  return slave_stream_;
}

const ConnectionContext::NpnState ConnectionContext::npn_state() const {
  DCHECK(!is_slave());
  return npn_state_;
}

void ConnectionContext::set_npn_state(NpnState state) {
  DCHECK(!is_slave());
  npn_state_ = state;
}

bool ConnectionContext::is_assuming_spdy() const {
  DCHECK(!is_slave());
  return assume_spdy_;
}

void ConnectionContext::set_assume_spdy(bool assume) {
  DCHECK(!is_slave());
  assume_spdy_ = assume;
}

}  // namespace mod_spdy
