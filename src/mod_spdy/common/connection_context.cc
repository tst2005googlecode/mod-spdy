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

#include "net/spdy/spdy_framer.h"

namespace mod_spdy {

ConnectionContext::ConnectionContext()
    : output_framer_(new spdy::SpdyFramer) {}

ConnectionContext::~ConnectionContext() {}

void ConnectionContext::set_protocol(const char* name, size_t length) {
  protocol_.assign(name, length);
}

}  // namespace mod_spdy
