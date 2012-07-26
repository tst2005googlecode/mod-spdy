// Copyright 2012 Google Inc.
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

#include "mod_spdy/apache/slave_connection_context.h"

#include "base/logging.h"
#include "mod_spdy/common/spdy_stream.h"

namespace mod_spdy {

SlaveConnectionContext::SlaveConnectionContext(
    bool using_ssl, SpdyStream* slave_stream)
    : using_ssl_(using_ssl),
      slave_stream_(slave_stream) {
  DCHECK(slave_stream_);
}

SlaveConnectionContext::~SlaveConnectionContext() {}

SpdyStream* SlaveConnectionContext::slave_stream() const {
  DCHECK(slave_stream_ != NULL);
  return slave_stream_;
}

int SlaveConnectionContext::spdy_version() const {
  int spdy_version = slave_stream()->spdy_version();
  DCHECK_GT(spdy_version, 0);
  return spdy_version;
}

}  // namespace mod_spdy
