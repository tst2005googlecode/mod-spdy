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

#include "mod_spdy/apache/response_header_populator.h"

#include <string>

#include "base/string_number_conversions.h"  // for IntToString
#include "mod_spdy/common/protocol_util.h"
#include "net/spdy/spdy_protocol.h"

namespace {

int AddOneHeader(void* ptr, const char* key, const char* value) {
  spdy::SpdyHeaderBlock* headers = static_cast<spdy::SpdyHeaderBlock*>(ptr);
  mod_spdy::HeaderPopulatorInterface::MergeInHeader(key, value, headers);
  return 1;  // return zero to stop, or non-zero to continue iterating
}

}  // namespace

namespace mod_spdy {

ResponseHeaderPopulator::ResponseHeaderPopulator(request_rec* request)
    : request_(request) {}

void ResponseHeaderPopulator::Populate(spdy::SpdyHeaderBlock* headers) const {
  // Put all the HTTP headers into the SPDY header table.  Note that APR tables
  // are multimaps -- they can store multiple values for the same key (see
  // http://thomas.eibner.dk/apache/table.html for details), so AddOneHeader
  // may get called multiple times with the same key but different values, so
  // it must be prepared to merge duplicate headers.
  apr_table_t* table = request_->headers_out;
  apr_table_do(AddOneHeader,  // function to call on each key/value pair
               headers,       // void* to be passed as first arg to function
               table,         // the apr_table_t to iterate over
               // Varargs: zero or more char* keys to iterate over, followed by
               // NULL.  Passing no keys iterates over the whole table (which
               // is what we want), but we still need the NULL.
               NULL);

  // Now add the SPDY-specific required headers.
  HeaderPopulatorInterface::MergeInHeader(
      spdy::kStatus, base::IntToString(request_->status), headers);
  HeaderPopulatorInterface::MergeInHeader(
      spdy::kVersion, request_->protocol, headers);
  // Finally remove SPDY-ignored headers.
  headers->erase(http::kConnection);
  headers->erase(http::kKeepAlive);
}

}  // namespace mod_spdy
