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

#ifndef MOD_SPDY_APACHE_BRIGADE_OUTPUT_STREAM_H_
#define MOD_SPDY_APACHE_BRIGADE_OUTPUT_STREAM_H_

#include "third_party/apache_httpd/include/apr_buckets.h"

#include "base/basictypes.h"
#include "mod_spdy/common/output_stream_interface.h"

namespace mod_spdy {

// Implementation of OutputStreamInterface that appends data to the end of a
// bucket brigade.
class BrigadeOutputStream : public mod_spdy::OutputStreamInterface {
 public:
  explicit BrigadeOutputStream(apr_bucket_brigade* brigade);

  virtual bool Write(const char* data, size_t num_bytes);

 private:
  apr_bucket_brigade* const brigade_;

  DISALLOW_COPY_AND_ASSIGN(BrigadeOutputStream);
};

}  // namespace mod_spdy

#endif  // MOD_SPDY_APACHE_BRIGADE_OUTPUT_STREAM_H_
