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

#include "mod_spdy/apache/spdy_input_filter.h"

#include "mod_spdy/apache/http_stream_accumulator.h"
#include "mod_spdy/apache/input_filter_input_stream.h"
#include "mod_spdy/common/flip_frame_pump.h"
#include "mod_spdy/common/spdy_to_http_converter.h"
#include "net/flip/flip_framer.h"

namespace mod_spdy {

SpdyInputFilter::SpdyInputFilter(conn_rec *c)
    : input_(new InputFilterInputStream(c->pool,
                                        c->bucket_alloc)),
      http_accumulator_(new HttpStreamAccumulator(c->pool,
                                                  c->bucket_alloc)),
      framer_(new flip::FlipFramer()),
      converter_(new SpdyToHttpConverter(framer_.get(),
                                         http_accumulator_.get())),
      pump_(new FlipFramePump(input_.get(), framer_.get())) {
  framer_->set_visitor(converter_.get());
}

SpdyInputFilter::~SpdyInputFilter() {
}

apr_status_t SpdyInputFilter::Read(ap_filter_t *filter,
                                   apr_bucket_brigade *brigade,
                                   ap_input_mode_t mode,
                                   apr_read_type_e block,
                                   apr_off_t readbytes) {
  if (filter->c->aborted) {
    // From mod_ssl's ssl_io_filter_input in ssl_engine_io.c
    apr_bucket *bucket = apr_bucket_eos_create(filter->c->bucket_alloc);
    APR_BRIGADE_INSERT_TAIL(brigade, bucket);
    return APR_ECONNABORTED;
  }

  if (mode == AP_MODE_INIT) {
    // Nothing to do.
    return APR_SUCCESS;
  }

  if (http_accumulator_->IsEmpty()) {
    // If there's no data in the accumulator, attempt to pull more
    // data into it by driving the FlipFramePump. Note that this will
    // not alway succeed; if there is no data available from the next
    // filter (e.g. no data to be read from the socket) then the
    // accumulator will not be populated with new data.
    input_->set_filter(filter, block);
    pump_->PumpOneFrame();
    input_->clear_filter();
  }

  apr_status_t rv = http_accumulator_->Read(brigade, mode, block, readbytes);
  if (rv == APR_SUCCESS && http_accumulator_->IsEmpty()) {
    // TODO: not sure this CHECK is always valid. If there's data in the
    // input then we need to pump another frame until we can satisfy the
    // request.
    CHECK(input_->IsEmpty());

    // If we've drained the internal buffers, then we should return
    // the status code we received the last time we read from the next
    // filter.
    return input_->next_filter_rv();
  }
  return rv;
}

}  // namespace mod_spdy
