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
#include "net/spdy/spdy_protocol.h"
#include "mod_spdy/common/spdy_frame_queue.h"

namespace mod_spdy {

class SpdyFramePriorityQueue;

// Represents one stream of a SPDY connection.  This class is used to
// coordinate and pass SPDY frames between the SPDY-to-HTTP filter, the
// HTTP-to-SPDY filter, and the master SPDY connection thread.  This class is
// thread-safe, and in particular can be used concurrently by the stream thread
// and the connection thread.
class SpdyStream {
 public:
  SpdyStream(spdy::SpdyStreamId stream_id, spdy::SpdyPriority priority,
             SpdyFramePriorityQueue* output_queue);

  ~SpdyStream();

  // Get the ID for this SPDY stream.
  spdy::SpdyStreamId stream_id() const { return stream_id_; }

  // Return true if this stream has been aborted and should shut down.
  bool is_aborted() const;

  // Abort this stream.  This method returns immediately, and the thread
  // running the stream will stop as soon as possible.
  void Abort();

  // Provide a SPDY frame sent from the client.  This is to be called from the
  // master connection thread.  This method takes ownership of the frame
  // object.
  void PostInputFrame(spdy::SpdyFrame* frame);

  // Get a SPDY frame from the client and return true, or return false if no
  // frame is available.  If the block argument is true and no frame is
  // currently available, block until a frame becomes available or the stream
  // is aborted.  This is to be called from the stream thread.  The caller
  // gains ownership of the provided frame.
  bool GetInputFrame(bool block, spdy::SpdyFrame** frame);

  // Send a SPDY frame to the client.  This is to be called from the stream
  // thread.  This method takes ownership of the frame object.
  void SendOutputFrame(spdy::SpdyFrame* frame);

 private:
  const spdy::SpdyStreamId stream_id_;
  const spdy::SpdyPriority priority_;
  SpdyFrameQueue input_queue_;
  SpdyFramePriorityQueue* output_queue_;

  DISALLOW_COPY_AND_ASSIGN(SpdyStream);
};

}  // namespace mod_spdy

#endif  // MOD_SPDY_COMMON_SPDY_STREAM_H_
