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

#include "mod_spdy/common/spdy_connection.h"

#include "base/basictypes.h"
#include "base/logging.h"
#include "base/scoped_ptr.h"
#include "mod_spdy/common/connection_context.h"
#include "mod_spdy/common/spdy_connection_io.h"
#include "mod_spdy/common/spdy_server_config.h"
#include "mod_spdy/common/spdy_stream.h"
#include "mod_spdy/common/spdy_stream_task_factory.h"
#include "net/spdy/spdy_protocol.h"

namespace mod_spdy {

SpdyConnection::SpdyConnection(const SpdyServerConfig* config,
                               SpdyConnectionIO* connection_io,
                               SpdyStreamTaskFactory* task_factory,
                               Executor* executor)
    : config_(config),
      connection_io_(connection_io),
      task_factory_(task_factory),
      executor_(executor),
      connection_stopped_(false),
      already_sent_goaway_(false),
      last_client_stream_id_(0) {
  framer_.set_visitor(this);
}

SpdyConnection::~SpdyConnection() {}

void SpdyConnection::Run() {
  while (!connection_stopped_) {
    if (connection_io_->IsConnectionAborted()) {
      LOG(WARNING) << "Master connection was aborted.";
      break;
    }

    // Read available input data.  The SpdyConnectionIO will grab any available
    // data and push it into the SpdyFramer that we pass to it here; the
    // SpdyFramer, in turn, will call our OnControl/OnStreamFrameData methods
    // to report decoded frames.
    const SpdyConnectionIO::ReadStatus status =
        connection_io_->ProcessAvailableInput(&framer_);
    if (status == SpdyConnectionIO::READ_CONNECTION_CLOSED) {
      break;
    }

    // Send any pending output, one frame at a time.
    spdy::SpdyFrame* frame = NULL;
    while (output_queue_.Pop(&frame)) {
      // Each invocation of SendFrame will flush that one frame down the wire.
      // Might it be better to batch them together?  Actually, maybe not -- we
      // already try to make sure that each data frame is nicely-sized (~4k),
      // so flushing them one at a time might be reasonable.  But we may want
      // to revisit this.
      if (!SendFrame(frame)) {
        LOG(DFATAL) << "Failed to send SPDY frame.";
      }
    }

    // TODO(mdsteele): ************* THIS WHILE-LOOP IS BAD! **************
    // Right now, this loop will simply busy-wait, spinning between checking
    // for more input and checking for more output.  The right thing to do
    // would be to sleep until either more input or more output is available,
    // and then wake up.  The problem is that it's hard to know when more input
    // is available without invoking the input filter chain; moreover, we
    // cannot use separate threads for input and output, because in Apache the
    // input and output filter chains for a connection must be invoked by the
    // same thread.
    //
    // One possibility would be to grab the input socket object, and arrange to
    // block until either the socket is ready to read OR our output queue is
    // nonempty.  (Obviously we would abstract that away in SpdyConnectionIO
    // somehow.)
    //
    // Failing that, we could at least do a blocking read when there are no
    // active streams (and therefore no possibility of new output arriving).
    // That would at least prevent us from busy-looping when no communication
    // is happening but the client is still keeping the connection open.
    //
    // One way or the other though, we need to find a way to fix this method
    // before mod_spdy will be ready for prime time.
  }

  StopConnection();
}

void SpdyConnection::OnError(spdy::SpdyFramer* framer) {
  LOG(ERROR) << "SpdyFramer error: "
             << spdy::SpdyFramer::ErrorCodeToString(framer->error_code());
  StopConnection();
}

void SpdyConnection::OnControl(const spdy::SpdyControlFrame* frame) {
  switch (frame->type()) {
    case spdy::SYN_STREAM:
      HandleSynStream(
          *static_cast<const spdy::SpdySynStreamControlFrame*>(frame));
      break;
    case spdy::SYN_REPLY:
      HandleSynReply(
          *static_cast<const spdy::SpdySynReplyControlFrame*>(frame));
      break;
    case spdy::RST_STREAM:
      HandleRstStream(
          *static_cast<const spdy::SpdyRstStreamControlFrame*>(frame));
      break;
    case spdy::SETTINGS:
      HandleSettings(
          *static_cast<const spdy::SpdySettingsControlFrame*>(frame));
      break;
    case spdy::NOOP:
      // ignore NOOP frames
      break;
    case spdy::PING:
      HandlePing(*frame);
      break;
    case spdy::GOAWAY:
      HandleGoAway(*static_cast<const spdy::SpdyGoAwayControlFrame*>(frame));
      break;
    case spdy::HEADERS:
      HandleHeaders(*static_cast<const spdy::SpdyHeadersControlFrame*>(frame));
      break;
    default:
      // Ignore unknown control frames, as per the SPDY spec.
      LOG(WARNING) << "Unknown control frame (type=" << frame->type() << ")";
      break;
  }
}

void SpdyConnection::OnStreamFrameData(spdy::SpdyStreamId stream_id,
                                       const char* data, size_t length) {
  // Look up the stream to post the data to.  We need to lock when reading the
  // stream map, because one of the stream threads could call
  // RemoveStreamTask() at any time.
  {
    base::AutoLock autolock(stream_map_lock_);
    SpdyStreamMap::const_iterator iter = stream_map_.find(stream_id);
    if (iter != stream_map_.end()) {
      SpdyStream* stream = iter->second->stream();
      // Copy the data into an _uncompressed_ SPDY data frame and post it to
      // the stream's input queue.
      spdy::SpdyDataFlags flags =
          length == 0 ? spdy::DATA_FLAG_FIN : spdy::DATA_FLAG_NONE;
      // Note that we must still be holding stream_map_lock_ when we call this
      // method -- otherwise the stream may be deleted out from under us by the
      // StreamTaskWrapper destructor.  That's okay -- PostInputFrame is a
      // quick operation and won't block (for any appreciable length of time).
      stream->PostInputFrame(
          framer_.CreateDataFrame(stream_id, data, length, flags));
      return;
    }
  }

  // If the client sends data for a nonexistant stream, we must send a
  // RST_STREAM frame with error code INVALID_STREAM.  See
  // http://dev.chromium.org/spdy/spdy-protocol/spdy-protocol-draft2#TOC-Data-frames
  // Note that we release the mutex *before* sending the frame.
  SendRstStreamFrame(stream_id, spdy::INVALID_STREAM);
}

void SpdyConnection::HandleSynStream(
    const spdy::SpdySynStreamControlFrame& frame){
  // The SPDY spec requires us to ignore SYN_STREAM frames after sending a
  // GOAWAY frame.  See:
  // http://dev.chromium.org/spdy/spdy-protocol/spdy-protocol-draft2#TOC-GOAWAY
  if (already_sent_goaway_) {
    return;
  }

  const spdy::SpdyStreamId stream_id = frame.stream_id();

  // Client stream IDs must be odd-numbered.
  if (stream_id % 2 == 0) {
    LOG(WARNING) << "Client sent even stream ID (" << stream_id
                 << ").  Aborting connection.";
    SendRstStreamFrame(stream_id, spdy::PROTOCOL_ERROR);
    SendGoAwayFrame();
    StopConnection();
    return;
  }

  // Client stream IDs must increase monotonically.
  if (stream_id <= last_client_stream_id_) {
    LOG(WARNING) << "  bad stream id, aborting stream";
    AbortStream(stream_id, spdy::PROTOCOL_ERROR);
    return;
  }

  {
    // Lock the stream map before we start checking its size or adding a new
    // stream to it.  We need to lock when touching the stream map, because one
    // of the stream threads could call RemoveStreamTask() at any time.
    base::AutoLock autolock(stream_map_lock_);
    // We already checked that stream_id > last_client_stream_id_, so there
    // definitely shouldn't already be a stream with this ID in the map.
    DCHECK(stream_map_.count(stream_id) == 0);

    // Limit the number of simultaneous open streams; refuse the stream if
    // there are too many currently active streams.
    if (stream_map_.size() >= config_->max_streams_per_connection()) {
      SendRstStreamFrame(stream_id, spdy::REFUSED_STREAM);
      return;
    }

    // Initiate a new stream.
    last_client_stream_id_ = stream_id;
    const spdy::SpdyPriority priority = frame.priority();
    StreamTaskWrapper* task_wrapper =
        new StreamTaskWrapper(this, stream_id, priority);
    stream_map_[stream_id] = task_wrapper;
    // TODO(mdsteele): Do we need to decompress the frame even if we're going
    // to ignore the frame, to make sure that our header compression state
    // stays correct?
    task_wrapper->stream()->PostInputFrame(framer_.DecompressFrame(frame));
    executor_->AddTask(task_wrapper, priority);
  }
}

void SpdyConnection::HandleSynReply(
    const spdy::SpdySynReplyControlFrame& frame) {
  // TODO(mdsteele)
}

void SpdyConnection::HandleRstStream(
    const spdy::SpdyRstStreamControlFrame& frame) {
  const spdy::SpdyStreamId stream_id = frame.stream_id();
  switch (frame.status()) {
    // These are totally benign reasons to abort a stream, so just abort the
    // stream without a fuss.
    case spdy::REFUSED_STREAM:
    case spdy::CANCEL:
      AbortStreamSilently(stream_id);
      break;
    // If there was a PROTOCOL_ERROR, the connection is probably unrecoverable,
    // so just log an error and abort the connection.
    case spdy::PROTOCOL_ERROR:
      LOG(WARNING) << "Client sent RST_STREAM with PROTOCOL_ERROR for stream "
                   << stream_id << ".  Aborting connection.";
      SendGoAwayFrame();
      StopConnection();
      break;
    // For all other errors, abort the stream, but log a warning first.
    // TODO(mdsteele): Should we have special behavior for any other kinds of
    // errors?
    default:
      LOG(WARNING) << "Client sent RST_STREAM with status=" << frame.status()
                   <<" for stream " << stream_id << ".  Aborting connection.";
      AbortStreamSilently(stream_id);
      break;
  }
}

void SpdyConnection::HandleSettings(
    const spdy::SpdySettingsControlFrame& frame) {
  // TODO(mdsteele): For now, we ignore SETTINGS frames from the client.  Once
  // we implement server-push, we should at least pay attention to the
  // MAX_CONCURRENT_STREAMS setting from the client so that we don't overload
  // them.
}

void SpdyConnection::HandlePing(const spdy::SpdyControlFrame& frame) {
  // The SPDY spec requires the server to ignore even-numbered PING frames that
  // it did not initiate.  See:
  // http://dev.chromium.org/spdy/spdy-protocol/spdy-protocol-draft2#TOC-PING

  // TODO(mdsteele): Check the ping ID, and ignore if it's even.  The current
  // version of SpdyFramer we're using lacks a method to get the ping ID, so
  // we'll have to wait until we upgrade.

  // Any odd-numbered PING frame we receive was initiated by the client, and
  // should thus be echoed back, as per the SPDY spec.
  if (!connection_io_->SendFrameRaw(frame)) {
    LOG(ERROR) << "Failed to send PING reply.";
  }
}

void SpdyConnection::HandleGoAway(const spdy::SpdyGoAwayControlFrame& frame) {
  // TODO(mdsteele): For now I think we can mostly ignore GOAWAY frames, but
  // once we implement server-push we definitely need to take note of them.
}

void SpdyConnection::HandleHeaders(const spdy::SpdyHeadersControlFrame& frame){
  const spdy::SpdyStreamId stream_id = frame.stream_id();
  // Look up the stream to post the data to.  We need to lock when reading the
  // stream map, because one of the stream threads could call
  // RemoveStreamTask() at any time.
  {
    // TODO(mdsteele): This is pretty similar to the code in OnStreamFrameData.
    //   Maybe we can factor it out?
    base::AutoLock autolock(stream_map_lock_);
    SpdyStreamMap::const_iterator iter = stream_map_.find(stream_id);
    if (iter != stream_map_.end()) {
      SpdyStream* stream = iter->second->stream();
      stream->PostInputFrame(framer_.DecompressFrame(frame));
      return;
    }
  }

  // Note that we release the mutex *before* sending the frame.
  // TODO(mdsteele): Do we need to decompress the frame even if we're going to
  // ignore the frame, to make sure that our header compression state stays
  // correct?
  SendRstStreamFrame(stream_id, spdy::INVALID_STREAM);
}

// Compress (if necessary), send, and then delete the given frame object.
bool SpdyConnection::SendFrame(const spdy::SpdyFrame* frame) {
  scoped_ptr<const spdy::SpdyFrame> compressed_frame(frame);
  DCHECK(compressed_frame != NULL);
  if (framer_.IsCompressible(*frame)) {
    // First compress the original frame into a new frame object...
    const spdy::SpdyFrame* compressed = framer_.CompressFrame(*frame);
    // ...then delete the original frame object and replace it with the
    // compressed frame object.
    compressed_frame.reset(compressed);
  }

  if (compressed_frame == NULL) {
    LOG(DFATAL) << "frame compression failed";
    return false;
  }

  return connection_io_->SendFrameRaw(*compressed_frame);
}

bool SpdyConnection::SendGoAwayFrame() {
  already_sent_goaway_ = true;
  return SendFrame(spdy::SpdyFramer::CreateGoAway(last_client_stream_id_));
}

bool SpdyConnection::SendRstStreamFrame(spdy::SpdyStreamId stream_id,
                                        spdy::SpdyStatusCodes status) {
  return SendFrame(spdy::SpdyFramer::CreateRstStream(stream_id, status));
}

void SpdyConnection::StopConnection() {
  connection_stopped_ = true;
  // Abort all remaining streams.  We need to lock when reading the stream
  // map, because one of the stream threads could call RemoveStreamTask() at
  // any time.
  {
    base::AutoLock autolock(stream_map_lock_);
    for (SpdyStreamMap::const_iterator iter = stream_map_.begin();
         iter != stream_map_.end(); ++iter) {
      iter->second->stream()->Abort();
    }
  }
  // Stop all stream threads and tasks for this SPDY connection.  This will
  // block until all currently running stream tasks have exited, but since we
  // just aborted all streams, that should hopefully happen fairly soon.  Note
  // that we must release the lock before calling this, because each stream
  // will remove itself from the stream map as it shuts down.
  executor_->Stop();
}

// Abort the stream without sending anything to the client.
void SpdyConnection::AbortStreamSilently(spdy::SpdyStreamId stream_id) {
  // We need to lock when reading the stream map, because one of the stream
  // threads could call RemoveStreamTask() at any time.
  base::AutoLock autolock(stream_map_lock_);
  SpdyStreamMap::const_iterator iter = stream_map_.find(stream_id);
  if (iter != stream_map_.end()) {
    iter->second->stream()->Abort();
  }
}

// Send a RST_STREAM frame and then abort the stream.
void SpdyConnection::AbortStream(spdy::SpdyStreamId stream_id,
                                 spdy::SpdyStatusCodes status) {
  SendRstStreamFrame(stream_id, status);
  AbortStreamSilently(stream_id);
}

// Remove the StreamTaskWrapper from the stream map.  This is the only method
// of SpdyConnection that is ever called by another thread (specifically, it is
// called by the StreamTaskWrapper destructor, which is called by the executor,
// which presumably uses worker threads) -- it is because of this that we must
// lock the stream_map_lock_ whenever we touch the stream map or its contents.
void SpdyConnection::RemoveStreamTask(StreamTaskWrapper* task_wrapper) {
  // We need to lock when touching the stream map, in case the main connection
  // thread is currently in the middle of reading the stream map.
  base::AutoLock autolock(stream_map_lock_);
  const spdy::SpdyStreamId stream_id = task_wrapper->stream()->stream_id();
  DCHECK(stream_map_.count(stream_id) == 1);
  DCHECK(task_wrapper == stream_map_[stream_id]);
  stream_map_.erase(stream_id);
}

// This constructor is always called by the main connection thread, so we're
// safe to call spdy_connection_->task_factory_->NewStreamTask().  However,
// the other methods of this class (Run(), Cancel(), and the destructor) are
// liable to be called from other threads by the executor.
SpdyConnection::StreamTaskWrapper::StreamTaskWrapper(
    SpdyConnection* spdy_conn,
    spdy::SpdyStreamId stream_id,
    spdy::SpdyPriority priority)
    : spdy_connection_(spdy_conn),
      stream_(stream_id, priority, &spdy_connection_->output_queue_),
      subtask_(spdy_connection_->task_factory_->NewStreamTask(&stream_)) {}

SpdyConnection::StreamTaskWrapper::~StreamTaskWrapper() {
  // Remove this object from the SpdyConnection's stream map.
  spdy_connection_->RemoveStreamTask(this);
}

void SpdyConnection::StreamTaskWrapper::Run() {
  subtask_->CallRun();
}

void SpdyConnection::StreamTaskWrapper::Cancel() {
  subtask_->CallCancel();
}

}  // namespace mod_spdy
