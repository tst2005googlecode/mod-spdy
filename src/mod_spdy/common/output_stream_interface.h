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

#ifndef MOD_SPDY_OUTPUT_STREAM_INTERFACE_H_
#define MOD_SPDY_OUTPUT_STREAM_INTERFACE_H_

#include "base/basictypes.h"

namespace mod_spdy {

// Basic data output stream.
class OutputStreamInterface {
 public:
  OutputStreamInterface() {}
  virtual ~OutputStreamInterface() {}

  /**
   * Write the given data to the stream.
   * @return true iff the write was successful
   */
  virtual bool Write(const char *data, size_t num_bytes) = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(OutputStreamInterface);
};

}  // namespace mod_spdy

#endif  // MOD_SPDY_OUTPUT_STREAM_INTERFACE_H_
