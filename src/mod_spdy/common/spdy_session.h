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

#ifndef MOD_SPDY_COMMON_SPDY_SESSION_H_
#define MOD_SPDY_COMMON_SPDY_SESSION_H_

#include <map>
#include <queue>
#include <vector>

#include "base/basictypes.h"
#include "base/synchronization/lock.h"
#include "mod_spdy/common/executor.h"
#include "mod_spdy/common/spdy_frame_priority_queue.h"
#include "mod_spdy/common/spdy_stream.h"
#include "net/instaweb/util/public/function.h"
#include "net/spdy/buffered_spdy_framer.h"
#include "net/spdy/spdy_protocol.h"

namespace mod_spdy {

class Executor;
class SpdySessionIO;
class SpdyServerConfig;
class SpdyStreamTaskFactory;

// Represents a SPDY session with a client.  Given an Executor for processing
// individual SPDY streams, and a SpdySessionIO for communicating with the
// client (sending and receiving frames), this class takes care of implementing
// the SPDY protocol and responding correctly to various situations.
class SpdySession : public net::BufferedSpdyFramerVisitorInterface {
 public:
  // The SpdySession does _not_ take ownership of any of these arguments.
  SpdySession(int spdy_version,
              const SpdyServerConfig* config,
              SpdySessionIO* session_io,
              SpdyStreamTaskFactory* task_factory,
              Executor* executor);
  virtual ~SpdySession();

  // What SPDY version is being used for this session?
  // TODO(mdsteele): This method should be const, but it isn't beacuse
  //   BufferedSpdyFramer::protocol_version() isn't const, for no reason.
  int spdy_version() { framer_.protocol_version(); }

  // Process the session; don't return until the session is finished.
  void Run();

  // BufferedSpdyFramerVisitorInterface methods:
  virtual void OnError(int error_code);
  virtual void OnStreamError(net::SpdyStreamId stream_id,
                             const std::string& description);
  virtual void OnSynStream(const net::SpdySynStreamControlFrame& frame,
                           const linked_ptr<net::SpdyHeaderBlock>& headers);
  virtual void OnSynReply(const net::SpdySynReplyControlFrame& frame,
                          const linked_ptr<net::SpdyHeaderBlock>& headers);
  virtual void OnHeaders(const net::SpdyHeadersControlFrame& frame,
                         const linked_ptr<net::SpdyHeaderBlock>& headers);
  virtual void OnRstStream(const net::SpdyRstStreamControlFrame& frame);
  virtual void OnGoAway(const net::SpdyGoAwayControlFrame& frame);
  virtual void OnPing(const net::SpdyPingControlFrame& frame);
  virtual void OnWindowUpdate(const net::SpdyWindowUpdateControlFrame& frame);
  virtual void OnStreamFrameData(net::SpdyStreamId stream_id,
                                 const char* data, size_t len);
  virtual void OnSetting(net::SpdySettingsIds id, uint8 flags, uint32 value);

 private:
  // A helper class for wrapping tasks returned by
  // SpdyStreamTaskFactory::NewStreamTask().  Running or cancelling this task
  // simply runs/cancels the wrapped task; however, this object also keeps a
  // SpdyStream object, and on deletion, this will remove itself from the
  // SpdySession's list of active streams.
  class StreamTaskWrapper : public net_instaweb::Function {
   public:
    // This constructor, called by the main connection thread, will call
    // task_factory_->NewStreamTask() to produce the wrapped task.
    StreamTaskWrapper(SpdySession* spdy_session,
                      net::SpdyStreamId stream_id,
                      net::SpdyStreamId associated_stream_id,
                      net::SpdyPriority priority);
    virtual ~StreamTaskWrapper();

    SpdyStream* stream() { return &stream_; }

   protected:
    // net_instaweb::Function methods (our implementations of these simply
    // run/cancel the wrapped subtask):
    virtual void Run();
    virtual void Cancel();

   private:
    SpdySession* const spdy_session_;
    SpdyStream stream_;
    net_instaweb::Function* const subtask_;

    DISALLOW_COPY_AND_ASSIGN(StreamTaskWrapper);
  };

  typedef std::map<net::SpdyStreamId, StreamTaskWrapper*> SpdyStreamMap;

  // Validate and set the per-stream initial flow-control window size to the
  // new value.  Must be using SPDY v3 or later to call this method.
  void SetInitialWindowSize(uint32 new_init_window_size);

  // Send a single SPDY frame to the client, compressing it first if necessary.
  // Stop the session if the connection turns out to be closed.  This method
  // takes ownership of the passed frame and will delete it.
  void SendFrame(const net::SpdyFrame* frame);
  // Send the frame as-is (without taking ownership).  Stop the session if the
  // connection turns out to be closed.
  void SendFrameRaw(const net::SpdyFrame& frame);

  // Convenience methods to send specific types of control frames:
  void SendGoAwayFrame();
  void SendRstStreamFrame(net::SpdyStreamId stream_id,
                          net::SpdyStatusCodes status);
  void SendSettingsFrame();

  // Close down the whole session, post-haste.  Block until all stream threads
  // have shut down.
  void StopSession();
  // Abort the stream without sending anything to the client.
  void AbortStreamSilently(net::SpdyStreamId stream_id);
  // Send a RST_STREAM frame and then abort the stream.
  void AbortStream(net::SpdyStreamId stream_id, net::SpdyStatusCodes status);

  // Remove the given StreamTaskWrapper object from the stream map.  This is
  // the method of this class that might be called from another thread.
  // (Specifically, it is called by the StreamTaskWrapper destructor, which is
  // called by the executor).
  void RemoveStreamTask(StreamTaskWrapper* stream_data);

  // Grab the stream_map_lock_ and check if stream_map_ is empty.
  bool StreamMapIsEmpty();

  // These fields are accessed only by the main connection thread, so they need
  // not be protected by a lock:
  const SpdyServerConfig* const config_;
  SpdySessionIO* const session_io_;
  SpdyStreamTaskFactory* const task_factory_;
  Executor* const executor_;
  net::BufferedSpdyFramer framer_;
  bool no_more_reading_;  // don't read any more input from connection
  bool session_stopped_;  // StopSession() has been called
  bool already_sent_goaway_;  // GOAWAY frame has been sent
  net::SpdyStreamId last_client_stream_id_;
  int32 initial_window_size_;  // per-stream initial flow-control window size

  // The stream map must be protected by a lock, because each stream thread
  // will remove itself from the map (by calling RemoveStreamTask) when the
  // stream closes.  You MUST hold the lock to use the stream_map_ OR to use
  // any of the StreamTaskWrapper or SpdyStream objects contained therein
  // (e.g. to post a frame to the stream), otherwise the stream object may be
  // deleted by another thread while you're using it.  You should NOT be
  // holding the lock when you e.g. send a frame to the client, as that may
  // block for a long time.
  base::Lock stream_map_lock_;
  SpdyStreamMap stream_map_;

  // The output queue is also shared between all stream threads, but its class
  // is thread-safe, so it doesn't need additional synchronization.
  SpdyFramePriorityQueue output_queue_;

  DISALLOW_COPY_AND_ASSIGN(SpdySession);
};

}  // namespace mod_spdy

#endif  // MOD_SPDY_COMMON_SPDY_SESSION_H_
