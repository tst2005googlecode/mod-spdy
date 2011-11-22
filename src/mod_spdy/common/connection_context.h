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

#ifndef MOD_SPDY_CONNECTION_CONTEXT_H_
#define MOD_SPDY_CONNECTION_CONTEXT_H_

#include <string>

#include "base/basictypes.h"
#include "base/scoped_ptr.h"

namespace spdy {
class SpdyFramer;
}  // namespace spdy

namespace mod_spdy {

// Shared context object for a SPDY connection.
class ConnectionContext {
 public:
  ConnectionContext();
  ~ConnectionContext();

  const std::string& protocol() { return protocol_; }
  void set_protocol(const char* protocol_name, size_t length);

  // Return the SpdyFramer to be used by all output streams on this connection
  // (the framer includes the shared compression context for output headers).
  spdy::SpdyFramer* output_framer() { return output_framer_.get(); }

 private:
  std::string protocol_;
  scoped_ptr<spdy::SpdyFramer> output_framer_;

  DISALLOW_COPY_AND_ASSIGN(ConnectionContext);
};

}  // namespace mod_spdy

#endif  // MOD_SPDY_CONNECTION_CONTEXT_H_
