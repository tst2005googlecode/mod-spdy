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

#include "mod_spdy/apache/brigade_output_stream.h"

namespace mod_spdy {

BrigadeOutputStream::BrigadeOutputStream(apr_bucket_brigade* brigade)
    : brigade_(brigade) {}

bool BrigadeOutputStream::Write(const char* data, size_t num_bytes) {
  // Pass NULL for the flush function (and flush function context) here, to
  // indicate that apr_brigade_write shouldn't try to empty out the brigade,
  // even if we push a lot of data into it.  Note that apr_brigade_write
  // promises to always return APR_SUCCESS if we pass NULL for the flush
  // function, so we don't need to check the return value.
  apr_brigade_write(brigade_, NULL, NULL, data,
                    static_cast<apr_size_t>(num_bytes));
  return true;
}

}  // namespace mod_spdy
