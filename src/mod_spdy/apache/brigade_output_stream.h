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

#include "third_party/apache_httpd/include/apr_buckets.h"
#include "third_party/apache_httpd/include/util_filter.h"

#include "base/basictypes.h"
#include "mod_spdy/common/output_stream_interface.h"

namespace mod_spdy {

// Implementation of OutputStreamInterface that writes into a bucket brigade
// using Apache's stdio-style brigade functions (see TAMB 8.11)
class BrigadeOutputStream : public mod_spdy::OutputStreamInterface {
 public:
  BrigadeOutputStream(ap_filter_t* filter,
                      apr_bucket_brigade* brigade);

  virtual bool Write(const char* data, size_t num_bytes);

 private:
  ap_filter_t* const filter_;
  apr_bucket_brigade* const brigade_;

  DISALLOW_COPY_AND_ASSIGN(BrigadeOutputStream);
};

}  // namespace mod_spdy
