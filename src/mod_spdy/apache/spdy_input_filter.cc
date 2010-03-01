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

#include <queue>

#include "mod_spdy/apache/http_stream_accumulator.h"
#include "mod_spdy/apache/input_filter_input_stream.h"
#include "mod_spdy/common/spdy_frame_pump.h"
#include "mod_spdy/common/spdy_stream_distributor.h"
#include "mod_spdy/common/spdy_to_http_converter.h"
#include "net/spdy/spdy_framer.h"

namespace mod_spdy {

// The SpdyToHttpConverterFactory creates SpdyToHttpConverter
// instances that write to an HttpStreamAccumulator. Each
// SpdyToHttpConverter gets its own dedicated HttpStreamAccumulator
// which is owned by the SpdyToHttpConverterFactory instance and
// placed in a queue. The SpdyToHttpConverterFactory also exposes a
// Read() method to drain the HttpStreamAccumulator instances. The
// HttpStreamAccumulators are drained in FIFO order. This is a
// sub-optimal implementation but it's the best we can do in a
// non-multiplexed environment. Because this is sub-optimal, we hide
// the class declaration inside this cc file instead of promoting it
// to a public header file.
class SpdyToHttpConverterFactory
    : public mod_spdy::SpdyFramerVisitorFactoryInterface {
 public:
  SpdyToHttpConverterFactory(spdy::SpdyFramer *framer,
                             apr_pool_t *pool,
                             apr_bucket_alloc_t *bucket_alloc);
  virtual ~SpdyToHttpConverterFactory();

  virtual spdy::SpdyFramerVisitorInterface *Create(
      spdy::SpdyStreamId stream_id);

  bool IsDataAvailable() const;

  bool HasError() const;

  // Read from the HttpStreamAccumulator queue. We read from the first
  // HttpStreamAccumulator in the queue, and do not begin reading from
  // the next HttpStreamAccumulator until the current
  // HttpStreamAccumulator is complete and empty.
  apr_status_t Read(apr_bucket_brigade *brigade,
                    ap_input_mode_t mode,
                    apr_read_type_e block,
                    apr_off_t readbytes);

 private:
  typedef std::queue<mod_spdy::HttpStreamAccumulator*> AccumulatorQueue;

  AccumulatorQueue queue_;
  spdy::SpdyFramer *const framer_;
  apr_pool_t *const pool_;
  apr_bucket_alloc_t *const bucket_alloc_;
};

SpdyToHttpConverterFactory::SpdyToHttpConverterFactory(
    spdy::SpdyFramer *framer, apr_pool_t *pool, apr_bucket_alloc_t *bucket_alloc)
    : framer_(framer), pool_(pool), bucket_alloc_(bucket_alloc) {
}

SpdyToHttpConverterFactory::~SpdyToHttpConverterFactory() {
  while (!queue_.empty()) {
    mod_spdy::HttpStreamAccumulator *accumulator = queue_.front();
    queue_.pop();
    delete accumulator;
  }
}

spdy::SpdyFramerVisitorInterface *SpdyToHttpConverterFactory::Create(
    spdy::SpdyStreamId stream_id) {
  mod_spdy::HttpStreamAccumulator *accumulator =
      new mod_spdy::HttpStreamAccumulator(pool_, bucket_alloc_);
  queue_.push(accumulator);
  return new mod_spdy::SpdyToHttpConverter(framer_, accumulator);
}

bool SpdyToHttpConverterFactory::IsDataAvailable() const {
  if (queue_.size() == 0) {
    return false;
  }
  mod_spdy::HttpStreamAccumulator *accumulator = queue_.front();
  if (accumulator->HasError()) {
    return false;
  }
  const bool is_empty = accumulator->IsEmpty();
  if (is_empty) {
    // There should never be an HttpStreamAccumulator in the queue
    // that's both empty and complete (it should be removed during a
    // call to Read()).
    DCHECK(!accumulator->IsComplete());
  }
  return !is_empty;
}

bool SpdyToHttpConverterFactory::HasError() const {
  if (queue_.size() == 0) {
    return false;
  }
  mod_spdy::HttpStreamAccumulator *accumulator = queue_.front();
  return accumulator->HasError();
}

apr_status_t SpdyToHttpConverterFactory::Read(apr_bucket_brigade *brigade,
                                              ap_input_mode_t mode,
                                              apr_read_type_e block,
                                              apr_off_t readbytes) {
  if (HasError()) {
    DCHECK(false);
    return APR_EGENERAL;
  }

  if (!IsDataAvailable()) {
    // TODO: return value needs to match what would be returned from
    // core_filters.c!
    return APR_SUCCESS;
  }
  mod_spdy::HttpStreamAccumulator *accumulator = queue_.front();
  apr_status_t rv = accumulator->Read(brigade, mode, block, readbytes);
  if (accumulator->IsComplete() && accumulator->IsEmpty()) {
    queue_.pop();
    delete accumulator;
  }
  return rv;
}

SpdyInputFilter::SpdyInputFilter(conn_rec *c)
    : input_(new InputFilterInputStream(c->pool,
                                        c->bucket_alloc)),
      framer_(new spdy::SpdyFramer()),
      factory_(new SpdyToHttpConverterFactory(framer_.get(),
                                              c->pool,
                                              c->bucket_alloc)),
      distributor_(new mod_spdy::SpdyStreamDistributor(framer_.get(),
                                                       factory_.get())),
      pump_(new SpdyFramePump(input_.get(), framer_.get())) {
  framer_->set_visitor(distributor_.get());
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

  input_->set_filter(filter, block);
  while (!factory_->HasError() && !factory_->IsDataAvailable()) {
    // If there's no data in the accumulator, attempt to pull more
    // data into it by driving the SpdyFramePump. Note that this will
    // not alway succeed; if there is no data available from the next
    // filter (e.g. no data to be read from the socket) then the
    // accumulator will not be populated with new data.
    if (!pump_->PumpOneFrame()) {
      break;
    }
  }
  input_->clear_filter();

  if (factory_->HasError()) {
    apr_bucket *bucket = apr_bucket_eos_create(filter->c->bucket_alloc);
    APR_BRIGADE_INSERT_TAIL(brigade, bucket);
    return APR_EGENERAL;
  }

  apr_status_t rv = factory_->Read(brigade, mode, block, readbytes);
  if (rv == APR_SUCCESS && !factory_->IsDataAvailable()) {
    DCHECK(input_->IsEmpty());

    // If we've drained the internal buffers, then we should return
    // the status code we received the last time we read from the next
    // filter.
    return input_->next_filter_rv();
  }
  return rv;
}

}  // namespace mod_spdy
