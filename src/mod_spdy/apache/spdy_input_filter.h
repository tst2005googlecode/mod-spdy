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

#ifndef MOD_SPDY_APACHE_SPDY_INPUT_FILTER_H_
#define MOD_SPDY_APACHE_SPDY_INPUT_FILTER_H_

#include "apr_buckets.h"
#include "util_filter.h"

#include "base/basictypes.h"
#include "base/scoped_ptr.h"

#include "mod_spdy/common/connection_context.h"

namespace spdy {
class SpdyFramer;
}  // namespace spdy

namespace mod_spdy {

class SpdyFramePump;
class InputFilterInputStream;
class SpdyStreamDistributor;
class SpdyToHttpConverterFactory;

class SpdyInputFilter {
 public:
  SpdyInputFilter(conn_rec *c, ConnectionContext* context);
  ~SpdyInputFilter();

  // Read data from the given filter, into the given brigade. This
  // method is responsible for driving the SPDY to HTTP conversion
  // process, by invoking the SpdyFramePump if necessary, and then
  // reading HTTP data from the HttpStreamAccumulator.
  apr_status_t Read(ap_filter_t *filter,
                    apr_bucket_brigade *brigade,
                    ap_input_mode_t mode,
                    apr_read_type_e block,
                    apr_off_t readbytes);

 private:
  scoped_ptr<InputFilterInputStream> input_;
  scoped_ptr<spdy::SpdyFramer> framer_;
  scoped_ptr<SpdyToHttpConverterFactory> factory_;
  scoped_ptr<mod_spdy::SpdyStreamDistributor> distributor_;
  scoped_ptr<SpdyFramePump> pump_;
  ConnectionContext* const context_;
  apr_bucket_brigade* const temp_brigade_;

  DISALLOW_COPY_AND_ASSIGN(SpdyInputFilter);
};

}  // namespace mod_spdy

#endif  // MOD_SPDY_APACHE_SPDY_INPUT_FILTER_H_
