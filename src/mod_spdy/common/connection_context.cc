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
      slave_stream_(NULL) {}

ConnectionContext::ConnectionContext(SpdyStream* slave_stream)
    : npn_state_(USING_SPDY),
      slave_stream_(slave_stream) {}

ConnectionContext::~ConnectionContext() {}

SpdyStream* ConnectionContext::slave_stream() const {
  DCHECK(is_slave());
  DCHECK(slave_stream_ != NULL);
  return slave_stream_;
}

void ConnectionContext::set_npn_state(NpnState state) {
  DCHECK(!is_slave());
  npn_state_ = state;
}

}  // namespace mod_spdy
