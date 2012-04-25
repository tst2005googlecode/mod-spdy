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

#include "base/basictypes.h"
#include "httpd.h"  // for request_rec

#include "mod_spdy/common/header_populator_interface.h"

namespace mod_spdy {

// Implementation of HeaderPopulatorInterface that uses the headers_out of a
// request_rec as a source.
class ResponseHeaderPopulator : public HeaderPopulatorInterface {
 public:
  explicit ResponseHeaderPopulator(request_rec* request);

  virtual void Populate(net::SpdyHeaderBlock* headers) const;

 private:
  request_rec* const request_;

  DISALLOW_COPY_AND_ASSIGN(ResponseHeaderPopulator);
};

}  // namespace mod_spdy
