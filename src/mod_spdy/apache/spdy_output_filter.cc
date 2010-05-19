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

// There are a number of things that every output filter should do, according
// to <http://httpd.apache.org/docs/2.3/developer/output-filters.html>.  In
// short, these things are:
//
//   - Respect FLUSH and EOS metadata buckets, and pass other metadata buckets
//     down the chain.  Ignore all buckets after an EOS.
//
//   - Don't allocate long-lived memory on every invokation.  In particular, if
//     you need a temp brigade, allocate it once and then reuse it each time.
//
//   - Never pass an empty brigade down the chain, but be ready to accept one
//     and do nothing.
//
//   - Calling apr_brigade_destroy can be dangerous; prefer using
//     apr_brigade_cleanup instead.
//
//   - Don't read the entire brigade into memory at once; the brigade may, for
//     example, contain a FILE bucket representing a 42 GB file.  Instead, use
//     apr_bucket_read to read a reasonable portion of the bucket, put the
//     resulting (small) bucket into a temp brigade, pass it down the chain,
//     and then clean up the temp brigade before continuing.
//
//   - If a bucket is to be saved beyond the scope of the filter invokation
//     that first received it, it must be "set aside" using the
//     apr_bucket_setaside macro.
//
//   - When reading a bucket, first use a non-blocking read; if it fails with
//     APR_EAGAIN, send a FLUSH bucket down the chain, and then read the bucket
//     with a blocking read.
//
// This code attempts to follow these rules.

#include "mod_spdy/apache/spdy_output_filter.h"

#include "base/string_util.h"  // For StringToInt64

#include "mod_spdy/apache/brigade_output_stream.h"
#include "mod_spdy/apache/response_header_populator.h"
#include "mod_spdy/common/connection_context.h"
#include "mod_spdy/common/output_filter_context.h"

namespace {

// This is the number of bytes we want to send per data frame.  We never send
// data frames larger than this, but we might send smaller ones if we have to
// flush early.
// TODO The SPDY folks say that smallish (~4kB) data frames are good; however,
//      we should experiment later on to see what value here performs the best.
const size_t kTargetDataFrameBytes = 4096;

bool GetRequestStreamId(request_rec* request, spdy::SpdyStreamId *out) {
  apr_table_t* headers = request->headers_in;
  // TODO: The "x-spdy-stream-id" string really ought to be stored in a shared
  //       constant somewhere.  But where?  Anyway, that issue may be obviated
  //       if and when we find a better way to communicate the stream ID from
  //       the input filter to the output filter.
  const char* value = apr_table_get(headers, "x-spdy-stream-id");
  if (value == NULL) {
    LOG(DFATAL) << "Request had no x-spdy-stream-id header.";
    return false;
  }
  int64 id = 0;
  if (StringToInt64(value, &id)) {
    *out = static_cast<spdy::SpdyStreamId>(id);
    return true;
  }

  LOG(DFATAL) << "Couldn't parse x-spdy-stream-id: " << value;
  return false;
}

}  // namespace

namespace mod_spdy {

SpdyOutputFilter::SpdyOutputFilter(ConnectionContext* conn_context,
                                   request_rec* request)
    : context_(new OutputFilterContext(conn_context)),
      output_brigade_(apr_brigade_create(request->pool,
                                         request->connection->bucket_alloc)),
      metadata_brigade_(apr_brigade_create(request->pool,
                                           request->connection->bucket_alloc)),
      end_of_stream_(false) {}

SpdyOutputFilter::~SpdyOutputFilter() {}

// Execute the given expression; if it returns a status code other than
// APR_SUCCESS, log an error and return from the current function with that
// status code as a value.
#define RETURN_IF_NOT_SUCCESS(expr)                                  \
  do {                                                               \
    const apr_status_t status(expr);                                 \
    if (status != APR_SUCCESS) {                                     \
      LOG(ERROR) << #expr << " failed with status code " << status;  \
      return status;                                                 \
    }                                                                \
  } while (false)

apr_status_t SpdyOutputFilter::Write(ap_filter_t* filter,
                                     apr_bucket_brigade* input_brigade) {
  // According to the page at
  //   http://httpd.apache.org/docs/2.3/developer/output-filters.html
  // we should never pass an empty brigade down the chain, but to be safe, we
  // should be prepared to accept one and do nothing.
  if (APR_BRIGADE_EMPTY(input_brigade)) {
    LOG(WARNING) << "SpdyOutputFilter::Write received an empty brigade.";
    return APR_SUCCESS;
  }

  // We shouldn't be getting any more buckets after an EOS, but if we do, we're
  // supposed to ignore them.
  if (end_of_stream_) {
    LOG(ERROR) << "SpdyOutputFilter::Write was called after EOS";
    return APR_SUCCESS;
  }

  request_rec* const request = filter->r;

  for (apr_bucket* bucket = APR_BRIGADE_FIRST(input_brigade);
       bucket != APR_BRIGADE_SENTINEL(input_brigade);
       bucket = APR_BUCKET_NEXT(bucket)) {
    if (APR_BUCKET_IS_METADATA(bucket)) {
      if (APR_BUCKET_IS_EOS(bucket)) {
        // EOS bucket -- there should be no more buckets in this stream.
        if (APR_BUCKET_NEXT(bucket) != APR_BRIGADE_SENTINEL(input_brigade)) {
          LOG(ERROR) << "SpdyOutputFilter::Write saw buckets after an EOS";
        }
        end_of_stream_ = true;
        return Send(filter, false);
      } else if (APR_BUCKET_IS_FLUSH(bucket)) {
        // FLUSH bucket -- call Send() immediately and send a FLUSH bucket.
        RETURN_IF_NOT_SUCCESS(Send(filter, true));
      } else {
        // Unknown metadata bucket -- send it next time we call Send().
        APR_BUCKET_REMOVE(bucket);
        RETURN_IF_NOT_SUCCESS(apr_bucket_setaside(bucket, request->pool));
        APR_BRIGADE_INSERT_TAIL(metadata_brigade_, bucket);
      }
    }
    // Ignore data buckets that represent HTTP headers.
    // N.B. The sent_bodyct field is not really documented (it seems to be
    // reserved for the use of core filters) but it seems to do what we want.
    // It starts out as 0, and is set to 1 by the core HTTP_HEADER filter to
    // indicate when body data has begun to be sent.
    else if (request->sent_bodyct) {
      // Data bucket -- get ready to read.
      const char* data = NULL;
      apr_size_t data_length = 0;

      // First, try a non-blocking read.
      const apr_status_t status = apr_bucket_read(bucket, &data, &data_length,
                                                  APR_NONBLOCK_READ);
      if (status == APR_SUCCESS) {
        data_buffer_.append(data, static_cast<size_t>(data_length));
      } else if (APR_STATUS_IS_EAGAIN(status)) {
        // Non-blocking read failed with EAGAIN, so do a blocking read (but
        // flush first, with a FLUSH bucket, in case we block for a long time).
        RETURN_IF_NOT_SUCCESS(Send(filter, true));
        RETURN_IF_NOT_SUCCESS(apr_bucket_read(bucket, &data, &data_length,
                                              APR_BLOCK_READ));
        data_buffer_.append(data, static_cast<size_t>(data_length));
      } else {
        return status;  // failure
      }

      // If we've buffered enough data to be worth sending a SPDY data frame,
      // do so.
      if (data_buffer_.size() >= kTargetDataFrameBytes) {
        RETURN_IF_NOT_SUCCESS(Send(filter, false));
      }
    }
  }

  // If we haven't sent the headers yet (because we've never called Send()),
  // then go ahead and do that now; otherwise, we're done.
  if (!context_->headers_have_been_sent()) {
    return Send(filter, false);
  } else {
    return APR_SUCCESS;
  }
}

apr_status_t SpdyOutputFilter::Send(ap_filter_t* filter,
                                    bool send_flush_bucket) {
  // TODO: It would be better to store the stream ID in the OutputFilterContext
  //       object, so that we don't have to reparse it here every time.
  spdy::SpdyStreamId stream_id = 0;
  if (!GetRequestStreamId(filter->r, &stream_id)) {
    return APR_EGENERAL;
  }

  // Prepare a fresh output stream.
  RETURN_IF_NOT_SUCCESS(apr_brigade_cleanup(output_brigade_));
  BrigadeOutputStream output_stream(output_brigade_);
  bool headers_fin = false;  // true if we sent a SYN_REPLY frame with FLAG_FIN

  // Send headers if we haven't yet.
  if (!context_->headers_have_been_sent()) {
    ResponseHeaderPopulator populator(filter->r);
    headers_fin = end_of_stream_ && data_buffer_.empty();
    context_->SendHeaders(stream_id, populator, headers_fin, &output_stream);
    data_buffer_.clear();
  }

  // If we have (strictly) more than one frame's worth of data waiting, send it
  // down the filter chain, kTargetDataFrameBytes bytes at a time.  If we are
  // left with _exactly_ kTargetDataFrameBytes bytes of data, we'll deal with
  // that in the next code block (see the comment there to explain why).
  if (data_buffer_.size() > kTargetDataFrameBytes) {
    const char* start = data_buffer_.data();
    size_t size = data_buffer_.size();
    while (size > kTargetDataFrameBytes) {
      context_->SendData(stream_id, start, kTargetDataFrameBytes,
                         false, &output_stream);
      RETURN_IF_NOT_SUCCESS(ap_pass_brigade(filter->next, output_brigade_));
      RETURN_IF_NOT_SUCCESS(apr_brigade_cleanup(output_brigade_));
      start += kTargetDataFrameBytes;
      size -= kTargetDataFrameBytes;
    }
    data_buffer_.erase(0, data_buffer_.size() - size);
  }

  // We need to send a data frame with _all_ the rest of our buffered data if:
  //   1) we're at the end of the stream and haven't yet sent a FLAG_FIN,
  //   2) we're supposed to flush and the buffer is nonempty, or
  //   3) we have (at least) a full data frame's worth in the buffer.
  //
  // Note that because of the previous code block, condition (3) will only be
  // true if we have exactly kTargetDataFrameBytes of data (hence the DCHECK).
  // Dealing with this case here instead of in the above block makes it easier
  // to make sure we correctly set FLAG_FIN on the final data frame, which is
  // why the above block uses a strict, > comparison rather than a non-strict,
  // >= comparison.
  if ((end_of_stream_ && !headers_fin) ||
      (send_flush_bucket && !data_buffer_.empty()) ||
      data_buffer_.size() >= kTargetDataFrameBytes) {
    context_->SendData(stream_id, data_buffer_.data(), data_buffer_.size(),
                       end_of_stream_, &output_stream);
    data_buffer_.clear();
  }

  // Send any unknown metadata we've got lying around.
  APR_BRIGADE_CONCAT(output_brigade_, metadata_brigade_);
  DCHECK(APR_BRIGADE_EMPTY(metadata_brigade_));

  // Send a FLUSH bucket, if necessary.
  if (send_flush_bucket) {
    APR_BRIGADE_INSERT_TAIL(
        output_brigade_,
        apr_bucket_flush_create(output_brigade_->bucket_alloc));
  }

  // Send an EOS bucket, if necessary.
  if (end_of_stream_) {
    APR_BRIGADE_INSERT_TAIL(
        output_brigade_,
        apr_bucket_eos_create(output_brigade_->bucket_alloc));
  }

  // Avoid sending an empty brigade.
  if (APR_BRIGADE_EMPTY(output_brigade_)) {
    return APR_SUCCESS;
  }

  return ap_pass_brigade(filter->next, output_brigade_);
}

}  // namespace mod_spdy
