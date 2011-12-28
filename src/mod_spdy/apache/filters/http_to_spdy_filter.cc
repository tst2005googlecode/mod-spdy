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
//   - Don't allocate long-lived memory on every invocation.  In particular, if
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
//   - If a bucket is to be saved beyond the scope of the filter invocation
//     that first received it, it must be "set aside" using the
//     apr_bucket_setaside macro.
//
//   - When reading a bucket, first use a non-blocking read; if it fails with
//     APR_EAGAIN, send a FLUSH bucket down the chain, and then read the bucket
//     with a blocking read.
//
// This code attempts to follow these rules.

#include "mod_spdy/apache/filters/http_to_spdy_filter.h"

#include "apr_strings.h"

#include "base/logging.h"
#include "mod_spdy/apache/response_header_populator.h"
#include "mod_spdy/common/spdy_stream.h"
#include "mod_spdy/common/version.h"
#include "net/spdy/spdy_framer.h"
#include "net/spdy/spdy_protocol.h"

namespace {

const char* kModSpdyHeader = "x-mod-spdy";
const char* kModSpdyVersion = MOD_SPDY_VERSION_STRING "-" LASTCHANGE_STRING;

// This is the number of bytes we want to send per data frame.  We never send
// data frames larger than this, but we might send smaller ones if we have to
// flush early.
// TODO The SPDY folks say that smallish (~4kB) data frames are good; however,
//      we should experiment later on to see what value here performs the best.
const size_t kTargetDataFrameBytes = 4096;

}  // namespace

namespace mod_spdy {

HttpToSpdyFilter::HttpToSpdyFilter(SpdyStream* stream)
    : stream_(stream),
      headers_have_been_sent_(false),
      end_of_stream_reached_(false) {
  DCHECK(stream_ != NULL);
}

HttpToSpdyFilter::~HttpToSpdyFilter() {}

// Check if the SPDY stream has been aborted; if so, mark the connection object
// as having been aborted and return APR_ECONNABORTED.  Hopefully, this will
// convince Apache to shut down processing for this (slave) connection, thus
// allowing this stream's thread to complete and exit.
#define RETURN_IF_STREAM_ABORT(filter)                       \
  do {                                                       \
    if ((filter)->c->aborted || stream_->is_aborted()) {     \
      (filter)->c->aborted = true;                           \
      return APR_ECONNABORTED;                               \
    }                                                        \
  } while (false)

apr_status_t HttpToSpdyFilter::Write(ap_filter_t* filter,
                                     apr_bucket_brigade* input_brigade) {
  if (filter->next != NULL) {
    // Our expectation is that this output filter will be the last filter in
    // the chain -- this is a TRANSCODE filter, so the only filters likely to
    // come after this one are SSL (a CONNECTION filter) and core (a NETWORK
    // filter), both of which we have carefully disabled for the slave
    // connection.  We never pass data onto the next filter if there is one --
    // instead we just push SPDY frames onto the thread-safe queue that carries
    // them back to the master connection.  If there _are_ filters after this
    // one, they'll be ignored, but let's log a warning here so that we'll know
    // that something unexpected is going on.
    //
    // We do allow mod_logio ("log_input_output") to run after us
    // without generating a warning, since it is a passive filter that
    // doesn't modify the stream.
    if (apr_strnatcasecmp("log_input_output", filter->next->frec->name) != 0) {
      LOG(WARNING) << "HttpToSpdyFilter is not the last filter in the chain: "
                   << filter->next->frec->name;
    }
  }

  // According to the page at
  //   http://httpd.apache.org/docs/2.3/developer/output-filters.html
  // we should never pass an empty brigade down the chain, but to be safe, we
  // should be prepared to accept one and do nothing.
  if (APR_BRIGADE_EMPTY(input_brigade)) {
    LOG(WARNING) << "HttpToSpdyFilter::Write received an empty brigade.";
    return APR_SUCCESS;
  }

  // We shouldn't be getting any more buckets after an EOS, but if we do, we're
  // supposed to ignore them.
  if (end_of_stream_reached_) {
    LOG(ERROR) << "HttpToSpdyFilter::Write was called after EOS";
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
          LOG(ERROR) << "HttpToSpdyFilter::Write saw buckets after an EOS";
        }
        end_of_stream_reached_ = true;
        RETURN_IF_STREAM_ABORT(filter);
        Send(filter, false);
        return APR_SUCCESS;
      } else if (APR_BUCKET_IS_FLUSH(bucket)) {
        // FLUSH bucket -- call Send() immediately and flush the data buffer.
        RETURN_IF_STREAM_ABORT(filter);
        Send(filter, true);
      } else {
        // Unknown metadata bucket.  This bucket has no meaning to us, and
        // there's no further filter to pass it to, so we just ignore it.
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
      apr_status_t status = apr_bucket_read(bucket, &data, &data_length,
                                            APR_NONBLOCK_READ);
      if (status == APR_SUCCESS) {
        data_buffer_.append(data, static_cast<size_t>(data_length));
      } else if (APR_STATUS_IS_EAGAIN(status)) {
        // Non-blocking read failed with EAGAIN, so try again with a blocking
        // read (but flush first, in case we block for a long time).
        RETURN_IF_STREAM_ABORT(filter);
        Send(filter, true);
        status = apr_bucket_read(bucket, &data, &data_length, APR_BLOCK_READ);
        if (status != APR_SUCCESS) {
          LOG(ERROR) << "Blocking read failed with status code " << status;
          return status;
        }
        data_buffer_.append(data, static_cast<size_t>(data_length));
      } else {
        return status;  // failure
      }

      // If we've buffered enough data to be worth sending one or more SPDY
      // data frames, do so.
      if (data_buffer_.size() >= kTargetDataFrameBytes) {
        RETURN_IF_STREAM_ABORT(filter);
        Send(filter, false);
      }
    }
  }

  // If we haven't sent the headers yet (because we've never called Send()),
  // then go ahead and do that now; otherwise, we're done.
  if (!headers_have_been_sent_) {
    RETURN_IF_STREAM_ABORT(filter);
    Send(filter, false);
  }
  return APR_SUCCESS;
}

void HttpToSpdyFilter::Send(ap_filter_t* filter, bool flush) {
  // We'll set this to true if we send a SYN_REPLY frame with FLAG_FIN:
  bool headers_fin = false;

  // Send headers if we haven't yet.
  if (!headers_have_been_sent_) {
    ResponseHeaderPopulator populator(filter->r);
    headers_fin = end_of_stream_reached_ && data_buffer_.empty();
    SendHeaders(populator, headers_fin);
    headers_have_been_sent_ = true;
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
      SendData(start, kTargetDataFrameBytes, false);
      start += kTargetDataFrameBytes;
      size -= kTargetDataFrameBytes;
    }
    data_buffer_.erase(0, data_buffer_.size() - size);
  }

  // We may still have some leftover data.  We need to send another data frame
  // now (rather than waiting for a full kTargetDataFrameBytes) if:
  //   1) we're at the end of the stream and haven't yet sent a FLAG_FIN,
  //   2) we're supposed to flush and the buffer is nonempty, or
  //   3) we still have a full data frame's worth in the buffer.
  //
  // Note that because of the previous code block, condition (3) will only be
  // true if we have exactly kTargetDataFrameBytes of data.  However, dealing
  // with that case here instead of in the above block makes it easier to make
  // sure we correctly set FLAG_FIN on the final data frame, which is why the
  // above block uses a strict, > comparison rather than a non-strict, >=
  // comparison.
  if ((end_of_stream_reached_ && !headers_fin) ||
      (flush && !data_buffer_.empty()) ||
      data_buffer_.size() >= kTargetDataFrameBytes) {
    SendData(data_buffer_.data(), data_buffer_.size(), end_of_stream_reached_);
    data_buffer_.clear();
  }
}

void HttpToSpdyFilter::SendHeaders(const HeaderPopulatorInterface& populator,
                                   bool flag_fin) {
  spdy::SpdyHeaderBlock headers;
  populator.Populate(&headers);
  headers[kModSpdyHeader] = kModSpdyVersion;
  const spdy::SpdyControlFlags flags =
      flag_fin ? spdy::CONTROL_FLAG_FIN : spdy::CONTROL_FLAG_NONE;
  // Don't compress the headers in the frame here; it will be compressed later
  // by the master connection (which maintains the shared header compression
  // state for all streams).
  if (stream_->is_server_push()) {
    stream_->SendOutputFrame(framer_.CreateSynStream(
        stream_->stream_id(), stream_->associated_stream_id(),
        stream_->priority(),
        static_cast<spdy::SpdyControlFlags>(
            flags | spdy::CONTROL_FLAG_UNIDIRECTIONAL),
        false,  // false = don't use compression
        &headers));
  } else {
    stream_->SendOutputFrame(framer_.CreateSynReply(
        stream_->stream_id(), flags,
        false,  // false = don't use compression
        &headers));
  }
}

void HttpToSpdyFilter::SendData(const char* data, size_t size, bool flag_fin) {
  // Don't compress the data frame here; it will be compressed later by the
  // master connection.
  // TODO(mdsteele): Actually, maybe each stream could compress its own data
  //   frames, since each SPDY stream has its own data compression context.  We
  //   just need the master connection to do the header compression.
  const spdy::SpdyDataFlags flags =
      flag_fin ? spdy::DATA_FLAG_FIN : spdy::DATA_FLAG_NONE;
  stream_->SendOutputFrame(framer_.CreateDataFrame(
      stream_->stream_id(), data, size, flags));
}

}  // namespace mod_spdy
