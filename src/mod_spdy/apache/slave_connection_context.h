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

#ifndef MOD_SPDY_APACHE_SLAVE_CONNECTION_CONTEXT_H_
#define MOD_SPDY_APACHE_SLAVE_CONNECTION_CONTEXT_H_

#include <string>

#include "base/basictypes.h"
#include "base/memory/scoped_ptr.h"

namespace mod_spdy {

class SpdyStream;

// Context for a 'slave' connection in mod_spdy, used to represent a fetch
// of a given URL from within Apache (as opposed to outgoing SPDY session to
// the client, which has a ConnectionContext).
class SlaveConnectionContext {
 public:
  // Create a context object for a slave connection.  The context object does
  // _not_ take ownership of the stream object.
  SlaveConnectionContext(bool using_ssl, SpdyStream* slave_stream);

  ~SlaveConnectionContext();

  // Return true if the connection to the user is over SSL.  This is almost
  // always true, but may be false if we've been set to use SPDY for non-SSL
  // connections (for debugging).  Note that for a slave connection, this
  // refers to whether the master network connection is using SSL.
  bool is_using_ssl() const { return using_ssl_; }

  // Return the SpdyStream object associated with this slave connection.
  SpdyStream* slave_stream() const;

  // Return the SPDY version number we will be using.
  int spdy_version() const;

 private:
  const bool using_ssl_;
  SpdyStream* const slave_stream_;

  DISALLOW_COPY_AND_ASSIGN(SlaveConnectionContext);
};

}  // namespace mod_spdy

#endif  // MOD_SPDY_APACHE_SLAVE_CONNECTION_CONTEXT_H_
