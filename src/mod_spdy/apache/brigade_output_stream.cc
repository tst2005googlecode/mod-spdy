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

#include "base/logging.h"
#include "third_party/apache_httpd/include/util_filter.h"

namespace mod_spdy {

BrigadeOutputStream::BrigadeOutputStream(ap_filter_t* filter,
                                         apr_bucket_brigade* brigade)
    : filter_(filter), brigade_(brigade) {}

bool BrigadeOutputStream::Write(const char* data, size_t num_bytes) {
  const apr_status_t status = ap_fwrite(filter_, brigade_, data, num_bytes);
  if (status != APR_SUCCESS) {
    LOG(ERROR) << "Failed to write data to brigade ("
               << static_cast<unsigned long>(num_bytes)
               << " bytes). Status code: " << status;
  }
  return status == APR_SUCCESS;
}

}  // namespace mod_spdy
