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

#ifndef MOD_SPDY_COMMON_SPDY_STREAM_H_
#define MOD_SPDY_COMMON_SPDY_STREAM_H_

#include "base/basictypes.h"
#include "base/string_piece.h"
#include "base/synchronization/condition_variable.h"
#include "base/synchronization/lock.h"
#include "net/spdy/buffered_spdy_framer.h"
#include "net/spdy/spdy_protocol.h"
#include "mod_spdy/common/spdy_frame_queue.h"
#include "mod_spdy/common/spdy_server_push_interface.h"

namespace mod_spdy {

class SpdyFramePriorityQueue;

// Represents one stream of a SPDY connection.  This class is used to
// coordinate and pass SPDY frames between the SPDY-to-HTTP filter, the
// HTTP-to-SPDY filter, and the master SPDY connection thread.  This class is
// thread-safe, and in particular can be used concurrently by the stream thread
// and the connection thread.
class SpdyStream {
 public:
  // The SpdyStream object does *not* take ownership of any of these arguments.
  // The BufferedSpdyFramer object is used only for creating uncompressed
  // frames; its state will never by modified by the SpdyStream (unfortunately,
  // however, we do need to call some non-const methods on it that don't
  // actually mutate state, so we require a non-const pointer here).
  SpdyStream(net::SpdyStreamId stream_id,
             net::SpdyStreamId associated_stream_id_,
             net::SpdyPriority priority,
             int32 initial_window_size,
             SpdyFramePriorityQueue* output_queue,
             net::BufferedSpdyFramer* framer,
             SpdyServerPushInterface* pusher);
  ~SpdyStream();

  // What version of SPDY is being used for this connection?
  int spdy_version() const { return framer_->protocol_version(); }

  // Return true if this stream was initiated by the server, false if it was
  // initiated by the client.
  bool is_server_push() const;

  // Get the ID for this SPDY stream.
  net::SpdyStreamId stream_id() const { return stream_id_; }

  // Get the ID for the SPDY stream with which this one is associated.  By the
  // SPDY spec, if there is no associated stream, this will be zero.
  net::SpdyStreamId associated_stream_id() const {
    return associated_stream_id_;
  }

  // Get the priority of this stream.
  net::SpdyPriority priority() const { return priority_; }

  // Return true if this stream has been aborted and should shut down.
  bool is_aborted() const;

  // Abort this stream.  This method returns immediately, and the thread
  // running the stream will stop as soon as possible (if it is currently
  // blocked on the window size, it will be woken up).
  void AbortSilently();

  // Same as AbortSilently, but also sends a RST_STREAM frame for this stream.
  void AbortWithRstStream(net::SpdyStatusCodes status);

  // What is the current window size for this stream?  This is mostly useful
  // for debugging.
  int32 current_window_size() const;

  // This should be called by the connection thread to adjust the window size,
  // either due to receiving a WINDOW_UPDATE frame from the client, or from the
  // client changing the initial window size with a SETTINGS frame.  The delta
  // argument will usually be positive (WINDOW_UPDATE is always positive), but
  // *can* be negative (if the client reduces the window size with SETTINGS).
  //
  // This method should *not* be called by the stream thread; the SpdyStream
  // object will automatically take care of decreasing the window size for sent
  // data.
  void AdjustWindowSize(int32 delta);

  // Provide a SPDY frame sent from the client.  This is to be called from the
  // master connection thread.  This method takes ownership of the frame
  // object.
  void PostInputFrame(net::SpdyFrame* frame);

  // Get a SPDY frame from the client and return true, or return false if no
  // frame is available.  If the block argument is true and no frame is
  // currently available, block until a frame becomes available or the stream
  // is aborted.  This is to be called from the stream thread.  The caller
  // gains ownership of the provided frame.
  bool GetInputFrame(bool block, net::SpdyFrame** frame);

  // Send a SYN_STREAM frame to the client for this stream.  This may only be
  // called if is_server_push() is true.
  void SendOutputSynStream(const net::SpdyHeaderBlock& headers, bool flag_fin);

  // Send a SYN_REPLY frame to the client for this stream.  This may only be
  // called if is_server_push() is false.
  void SendOutputSynReply(const net::SpdyHeaderBlock& headers, bool flag_fin);

  // Send a HEADERS frame to the client for this stream.
  void SendOutputHeaders(const net::SpdyHeaderBlock& headers, bool flag_fin);

  // Send a WINDOW_UPDATE frame to the client for this stream, indicating that
  // we have consumed the given quantity of data.  The delta must be within the
  // legal range for window update frames (from 1 to 0x7fffffff).
  void SendOutputWindowUpdate(size_t delta);

  // Send a SPDY data frame to the client on this stream.
  void SendOutputDataFrame(base::StringPiece data, bool flag_fin);

  // Initiate a SPDY server push associated with this stream, roughly by
  // pretending that the client sent a SYN_STREAM with the given headers.  To
  // repeat: the headers argument is _not_ the headers that the server will
  // send to the client, but rather the headers to _pretend_ that the client
  // sent to the server.  Requires that spdy_version() >= 3.
  SpdyServerPushInterface::PushStatus StartServerPush(
      net::SpdyPriority priority,
      const net::SpdyHeaderBlock& request_headers);

 private:
  // Send a SPDY frame to the client.  This is to be called from the stream
  // thread.  This method takes ownership of the frame object.  Must be holding
  // lock_ to call this method.
  void SendOutputFrame(net::SpdyFrame* frame);

  // Aborts the input queue, sets aborted_, and wakes up threads waiting on
  // condvar_.  Must be holding lock_ to call this method.
  void InternalAbortSilently();

  // Like InternalAbortSilently, but also sends a RST_STREAM frame for this
  // stream.  Must be holding lock_ to call this method.
  void InternalAbortWithRstStream(net::SpdyStatusCodes status);

  const net::SpdyStreamId stream_id_;
  const net::SpdyStreamId associated_stream_id_;
  const net::SpdyPriority priority_;
  SpdyFrameQueue input_queue_;
  SpdyFramePriorityQueue* const output_queue_;
  net::BufferedSpdyFramer* const framer_;
  SpdyServerPushInterface* const pusher_;

  // The lock protects the fields below.  The above fields do not require
  // additional synchronization.
  mutable base::Lock lock_;
  base::ConditionVariable condvar_;
  int32 window_size_;
  bool aborted_;

  DISALLOW_COPY_AND_ASSIGN(SpdyStream);
};

}  // namespace mod_spdy

#endif  // MOD_SPDY_COMMON_SPDY_STREAM_H_
