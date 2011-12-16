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

class SpdyStream;

// Shared context object for a SPDY connection.
class ConnectionContext {
 public:
  enum NpnState {
    NOT_DONE_YET,
    USING_SPDY,
    NOT_USING_SPDY
  };

  // Create a context object for a non-slave connection.
  ConnectionContext();
  // Create a context object for a slave connection.  The context object does
  // _not_ take ownership of the stream object.
  explicit ConnectionContext(SpdyStream* slave_stream);

  ~ConnectionContext();

  // Return true if this is a slave connection representing a SPDY stream, or
  // false if it is a "real" connection.
  bool is_slave() const { return slave_stream_ != NULL; }

  // Return the SpdyStream object associated with this slave connection.
  // Requires that is_slave() is true.
  SpdyStream* slave_stream() const;

  // Get the NPN state of this connection.
  const NpnState npn_state() const { return npn_state_; }

  // Set the NPN state of this connection.  Requires that is_slave() is false.
  void set_npn_state(NpnState state);

  // Return the SpdyFramer to be used by all output streams on this connection
  // (the framer includes the shared compression context for output headers).
  // TODO(mdsteele): This is deprecated, to be removed when we switch over
  //   completely to the new multiplexing implementation.
  spdy::SpdyFramer* output_framer() const { return output_framer_.get(); }

 private:
  NpnState npn_state_;
  SpdyStream* const slave_stream_;
  scoped_ptr<spdy::SpdyFramer> output_framer_;  // deprecated, see above

  DISALLOW_COPY_AND_ASSIGN(ConnectionContext);
};

}  // namespace mod_spdy

#endif  // MOD_SPDY_CONNECTION_CONTEXT_H_
