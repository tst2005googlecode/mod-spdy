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

namespace mod_spdy {

class SpdyStream;

// Shared context object for a SPDY connection.
class ConnectionContext {
 public:
  // Create a context object for a non-slave connection.
  explicit ConnectionContext(bool using_ssl);
  // Create a context object for a slave connection.  The context object does
  // _not_ take ownership of the stream object.
  ConnectionContext(bool using_ssl, SpdyStream* slave_stream);

  ~ConnectionContext();

  // Return true if the connection to the user is over SSL.  This is almost
  // always true, but may be false if we've been set to use SPDY for non-SSL
  // connections (for debugging).  Note that for a slave connection, this
  // refers to whether the master network connection is using SSL.
  bool is_using_ssl() const { return using_ssl_; }

  // Return true if we are using SPDY for this connection, which is the case if
  // either 1) SPDY was chosen by NPN, or 2) we are assuming SPDY regardless of
  // NPN.  For slave connections, this always returns true.
  bool is_using_spdy() const;

  // Return true if this is a slave connection representing a SPDY stream, or
  // false if it is a "real" connection.
  bool is_slave() const { return slave_stream_ != NULL; }

  // Return the SpdyStream object associated with this slave connection.
  // Requires that is_slave() is true.
  SpdyStream* slave_stream() const;

  enum NpnState {
    // NOT_DONE_YET: NPN has not yet completed.
    NOT_DONE_YET,
    // USING_SPDY: We have agreed with the client to use SPDY for this
    // connection.
    USING_SPDY,
    // NOT_USING_SPDY: We have decided not to use SPDY for this connection.
    NOT_USING_SPDY
  };

  // Get the NPN state of this connection.  Unless you actually care about NPN
  // itself, you probably don't want to use this method to check if SPDY is
  // being used; instead, use is_using_spdy().  Requires that is_slave() is
  // false.
  const NpnState npn_state() const;

  // Set the NPN state of this connection.  Requires that is_slave() is false.
  void set_npn_state(NpnState state);

  // If true, we are simply _assuming_ SPDY, regardless of the outcome of NPN.
  // Requires that is_slave() is false.
  bool is_assuming_spdy() const;

  // Set whether we are assuming SPDY for this connection (regardless of NPN).
  // Requires that is_slave() is false.
  void set_assume_spdy(bool assume);

 private:
  const bool using_ssl_;
  NpnState npn_state_;
  bool assume_spdy_;
  SpdyStream* const slave_stream_;

  DISALLOW_COPY_AND_ASSIGN(ConnectionContext);
};

}  // namespace mod_spdy

#endif  // MOD_SPDY_CONNECTION_CONTEXT_H_
