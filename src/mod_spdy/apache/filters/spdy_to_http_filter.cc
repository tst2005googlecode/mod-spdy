// Copyright 2011 Google Inc.
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

#include "mod_spdy/apache/filters/spdy_to_http_filter.h"

#include <map>
#include <string>

#include "base/logging.h"
#include "base/scoped_ptr.h"
#include "base/string_piece.h"
#include "mod_spdy/common/http_stream_visitor_interface.h"
#include "mod_spdy/common/spdy_stream.h"
#include "mod_spdy/common/spdy_to_http_converter.h"
#include "net/spdy/spdy_frame_builder.h"
#include "net/spdy/spdy_framer.h"
#include "net/spdy/spdy_protocol.h"

namespace spdy { typedef std::map<std::string, std::string> SpdyHeaderBlock; }

namespace {

// If, during an AP_MODE_GETLINE read, we pull in this much data (or more)
// without seeing a linebreak, just give up and return what we have.
const size_t kGetlineThreshold = 4096;

// TODO(mdsteele): In more recent versions of net/spdy/, this is a static
//   method on SpdyFramer.  We should upgrade and use that instead of
//   duplicating it here.
bool ParseHeaderBlockInBuffer(const char* header_data,
                              size_t header_length,
                              spdy::SpdyHeaderBlock* block) {
  // Code from spdy_framer.cc:
  spdy::SpdyFrameBuilder builder(header_data, header_length);
  void* iter = NULL;
  uint16 num_headers;
  if (builder.ReadUInt16(&iter, &num_headers)) {
    int index;
    for (index = 0; index < num_headers; ++index) {
      std::string name;
      std::string value;
      if (!builder.ReadString(&iter, &name))
        break;
      if (!builder.ReadString(&iter, &value))
        break;
      if (!name.size() || !value.size())
        return false;
      if (block->find(name) == block->end()) {
        (*block)[name] = value;
      } else {
        return false;
      }
    }
    return index == num_headers &&
        iter == header_data + header_length;
  }
  return false;
}

class HttpStringVisitor : public mod_spdy::HttpStreamVisitorInterface {
 public:
  explicit HttpStringVisitor(std::string* str)
      : string_(str), has_error_(false) {}
  ~HttpStringVisitor() {}

  bool has_error() const { return has_error_; }

  // HttpStreamVisitorInterface methods:
  virtual void OnStatusLine(const char* method, const char* scheme,
                            const char* host, const char* path,
                            const char* version);
  virtual void OnHeader(const char* key, const char* value);
  virtual void OnHeadersComplete();
  virtual void OnBody(const char* data, size_t data_len);
  virtual void OnComplete() {}
  virtual void OnTerminate() { has_error_ = true; }

 private:
  std::string* const string_;
  bool has_error_;

  DISALLOW_COPY_AND_ASSIGN(HttpStringVisitor);
};

void HttpStringVisitor::OnStatusLine(const char* method, const char* scheme,
                                     const char* host, const char* path,
                                     const char* version) {
  string_->reserve(string_->size() + strlen(method) + strlen(path) +
                   strlen(version) + 4);
  string_->append(method);
  string_->push_back(' ');
  string_->append(path);
  string_->push_back(' ');
  string_->append(version);
  string_->append("\r\n");
}

void HttpStringVisitor::OnHeader(const char* key, const char* value) {
  string_->reserve(string_->size() + strlen(key) + strlen(value) + 4);
  string_->append(key);
  string_->append(": ");
  string_->append(value);
  string_->append("\r\n");
}

void HttpStringVisitor::OnHeadersComplete() {
  string_->append("\r\n");
}

void HttpStringVisitor::OnBody(const char* data, size_t size) {
  string_->append(data, size);
}

}  // namespace

namespace mod_spdy {

SpdyToHttpFilter::SpdyToHttpFilter(SpdyStream* stream)
    : stream_(stream),
      next_read_start_(0),
      end_of_stream_reached_(false) {
  DCHECK(stream_ != NULL);
}

SpdyToHttpFilter::~SpdyToHttpFilter() {}

// Macro to check if the SPDY stream has been aborted; if so, mark the
// connection object as having been aborted and return APR_ECONNABORTED.
// Hopefully, this will convince Apache to shut down processing for this
// (slave) connection, thus allowing this stream's thread to complete and exit.
//
// As an extra measure, we also insert an EOS bucket into the brigade before
// returning.  This idea comes from ssl_io_filter_input() in ssl_engine_io.c in
// mod_ssl, which does so with the following comment: "Ok, if we aborted, we
// ARE at the EOS.  We also have aborted.  This 'double protection' is probably
// redundant, but also effective against just about anything."
#define RETURN_IF_STREAM_ABORT(filter, brigade)                         \
  do {                                                                  \
    if ((filter)->c->aborted || stream_->is_aborted()) {                \
      (filter)->c->aborted = true;                                      \
      APR_BRIGADE_INSERT_TAIL(                                          \
          (brigade), apr_bucket_eos_create((filter)->c->bucket_alloc)); \
      return APR_ECONNABORTED;                                          \
    }                                                                   \
  } while (false)

apr_status_t SpdyToHttpFilter::Read(ap_filter_t *filter,
                                    apr_bucket_brigade *brigade,
                                    ap_input_mode_t mode,
                                    apr_read_type_e block,
                                    apr_off_t readbytes) {
  if (filter->next != NULL) {
    LOG(WARNING) << "SpdyToHttpFilter is not the last filter in the chain: "
                 << filter->next->frec->name;
  }

  // Clear any buffer data that was already returned on a previous invocation
  // of this filter.
  if (next_read_start_ > 0) {
    data_buffer_.erase(0, next_read_start_);
    next_read_start_ = 0;
  }

  // Check if this SPDY stream has been aborted, and if so, quit.  We will also
  // check for aborts just after each time we call GetNextFrame (that's a good
  // time to check, since a stream abort can interrupt a blocking call to
  // GetNextFrame).
  RETURN_IF_STREAM_ABORT(filter, brigade);

  // Keep track of how much data, if any, we should place into the brigade.
  int bytes_read = 0;

  // We don't need to do anything for AP_MODE_INIT.
  if (mode == AP_MODE_INIT) {
    return APR_SUCCESS;
  }
  // For AP_MODE_READBYTES and AP_MODE_SPECULATIVE, we try to read the quantity
  // of bytes we are asked for.  For AP_MODE_EXHAUSTIVE, we read as much as
  // possible.
  else if (mode == AP_MODE_READBYTES || mode == AP_MODE_SPECULATIVE ||
           mode == AP_MODE_EXHAUSTIVE) {
    // Try to get as much data as we were asked for.
    while (readbytes > data_buffer_.size() || mode == AP_MODE_EXHAUSTIVE) {
      const bool got_frame = GetNextFrame(block);
      RETURN_IF_STREAM_ABORT(filter, brigade);
      if (!got_frame) {
        break;
      }
    }

    // Put the data we read into a transient bucket.  We use a transient bucket
    // to avoid an extra string copy here.
    bytes_read = std::min(static_cast<int>(readbytes),
                          static_cast<int>(data_buffer_.size()));
  }
  // For AP_MODE_GETLINE, try to return a full text line of data.
  else if (mode == AP_MODE_GETLINE) {
    // Try to find the first linebreak in the remaining data stream.
    size_t linebreak = std::string::npos;
    size_t start = 0;
    while (true) {
      linebreak = data_buffer_.find('\n', start);
      // Stop if we find a linebreak, or if we've pulled too much data already.
      if (linebreak != std::string::npos ||
          data_buffer_.size() >= kGetlineThreshold) {
        break;
      }
      // Remember where we left off so we don't have to re-scan the whole
      // buffer on the next iteration.
      start = data_buffer_.size();
      // We haven't seen a linebreak yet, so try to get more data.
      const bool got_frame = GetNextFrame(block);
      RETURN_IF_STREAM_ABORT(filter, brigade);
      if (!got_frame) {
        break;
      }
    }

    // If we found a linebreak, return data up to and including that linebreak.
    // Otherwise, just send whatever we were able to get.
    bytes_read = (linebreak == std::string::npos ?
                  data_buffer_.size() : linebreak + 1);
  }
  // We don't support AP_MODE_EATCRLF.  Doing so would be tricky, and probably
  // totally pointless.  But if we ever decide to implement it, see
  // http://mail-archives.apache.org/mod_mbox/httpd-dev/200504.mbox/%3C1e86e5df78f13fcc9af02b3f5d749b33@ricilake.net%3E
  // for more information on its subtle semantics.
  else {
    DCHECK(mode == AP_MODE_EATCRLF);
    LOG(WARNING) << "Unsupported read mode: " << mode;
    return APR_ENOTIMPL;
  }

  // Keep track of whether we were able to put any buckets into the brigade.
  bool success = false;

  // If we managed to read any data, put it into the brigade.
  if (bytes_read > 0) {
    APR_BRIGADE_INSERT_TAIL(brigade, apr_bucket_transient_create(
        data_buffer_.data(), bytes_read, brigade->bucket_alloc));
    success = true;
  }

  // If this is the last bit of data from this stream, send an EOS bucket.
  if (end_of_stream_reached_ && bytes_read == data_buffer_.size()) {
    APR_BRIGADE_INSERT_TAIL(brigade, apr_bucket_eos_create(
        brigade->bucket_alloc));
    success = true;
  }

  // If this read failed and this was a non-blocking read, invite the caller to
  // try again.
  if (!success && block == APR_NONBLOCK_READ) {
    return APR_EAGAIN;
  }

  // Unless this is a speculative read, we should skip past the bytes we read
  // next time this filter is invoked.  We don't want to erase those bytes
  // yet, though, so that we can return them to the previous filter in a
  // transient bucket.
  if (mode != AP_MODE_SPECULATIVE) {
    next_read_start_ = bytes_read;
  }

  return APR_SUCCESS;
}

bool SpdyToHttpFilter::GetNextFrame(apr_read_type_e block) {
  // Try to get the next SPDY frame from the stream.
  scoped_ptr<spdy::SpdyFrame> frame;
  {
    spdy::SpdyFrame* frame_ptr = NULL;
    if (!stream_->GetInputFrame(block == APR_BLOCK_READ, &frame_ptr)) {
      DCHECK(frame_ptr == NULL);
      return false;
    }
    frame.reset(frame_ptr);
  }
  DCHECK(frame.get() != NULL);

  // Decode the frame into HTTP and append to the data buffer.
  if (frame->is_control_frame()) {
    spdy::SpdyControlFrame* ctrl_frame =
        static_cast<spdy::SpdyControlFrame*>(frame.get());
    switch (ctrl_frame->type()) {
      case spdy::SYN_STREAM:
        DecodeSynStream(
            *static_cast<spdy::SpdySynStreamControlFrame*>(ctrl_frame));
        break;
      // TODO(mdsteele): Handle HEADERS frames, and maybe others?
      default:
        // Other frame types should be handled by the master connection, rather
        // than sent here.
        LOG(DFATAL) << "Master connection sent a frame of type "
                    << ctrl_frame->type() << " to stream "
                    << stream_->stream_id();
        AbortStream(spdy::INTERNAL_ERROR);
        return false;
    }
  } else {
    spdy::SpdyDataFrame* data_frame =
        static_cast<spdy::SpdyDataFrame*>(frame.get());
    data_buffer_.append(data_frame->payload(), data_frame->length());
  }

  return true;
}

void SpdyToHttpFilter::DecodeSynStream(
    const spdy::SpdySynStreamControlFrame& frame) {
  spdy::SpdyHeaderBlock block;
  if (!ParseHeaderBlockInBuffer(frame.header_block(), frame.header_block_len(),
                                &block)) {
    LOG(ERROR) << "Invalid SYN_STREAM header block in stream "
               << stream_->stream_id();
    AbortStream(spdy::PROTOCOL_ERROR);
    return;
  }

  HttpStringVisitor visitor(&data_buffer_);
  if (!GenerateRequestLineFromHeaderBlock(block, &visitor)) {
    // TODO(mdsteeele): According to the SPDY spec, we're supposed to return an
    //   HTTP 400 (Bad Request) reply in this case.  We need to do some minor
    //   refactoring to make that possible.
    LOG(DFATAL) << "Could not generate request line from SYN_STREAM frame"
                << " in stream " << stream_->stream_id();
    AbortStream(spdy::INTERNAL_ERROR);
    return;
  }
  GenerateHeadersFromHeaderBlock(block, &visitor);
  visitor.OnHeadersComplete();
}

void SpdyToHttpFilter::AbortStream(spdy::SpdyStatusCodes status) {
  stream_->SendOutputFrame(spdy::SpdyFramer::CreateRstStream(
      stream_->stream_id(), status));
  stream_->Abort();
}

}  // namespace mod_spdy
