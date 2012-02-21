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
#include "mod_spdy/common/executor.h"
#include "mod_spdy/common/spdy_frame_priority_queue.h"
#include "mod_spdy/common/spdy_stream.h"
#include "net/instaweb/util/public/function.h"
#include "net/spdy/spdy_framer.h"
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
class SpdySession : public spdy::SpdyFramerVisitorInterface {
 public:
  // The SpdySession does _not_ take ownership of any of these arguments.
  SpdySession(const SpdyServerConfig* config,
                 SpdySessionIO* session_io,
                 SpdyStreamTaskFactory* task_factory,
                 Executor* executor);
  virtual ~SpdySession();

  // Process the session; don't return until the session is finished.
  void Run();

  // SpdyFramerVisitorInterface interface (note that we do _not_ take ownership
  // of any of the arguments of these methods):
  virtual void OnError(spdy::SpdyFramer* framer);
  virtual void OnControl(const spdy::SpdyControlFrame* frame);
  virtual void OnStreamFrameData(spdy::SpdyStreamId stream_id,
                                 const char* data, size_t length);

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
                      spdy::SpdyStreamId stream_id,
                      spdy::SpdyStreamId associated_stream_id,
                      spdy::SpdyPriority priority);
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

  typedef std::map<spdy::SpdyStreamId, StreamTaskWrapper*> SpdyStreamMap;

  // Handlers for specific types of control frames, dispatched from OnControl.
  // Note that these methods do _not_ gain ownership of the passed frame.
  void HandleSynStream(const spdy::SpdySynStreamControlFrame& frame);
  void HandleSynReply(const spdy::SpdySynReplyControlFrame& frame);
  void HandleRstStream(const spdy::SpdyRstStreamControlFrame& frame);
  void HandleSettings(const spdy::SpdySettingsControlFrame& frame);
  void HandlePing(const spdy::SpdyControlFrame& frame);
  void HandleGoAway(const spdy::SpdyGoAwayControlFrame& frame);
  void HandleHeaders(const spdy::SpdyHeadersControlFrame& frame);

  // Send a single SPDY frame to the client, compressing it first if necessary.
  // This method takes ownership of the passed frame and will delete it.
  bool SendFrame(const spdy::SpdyFrame* frame);

  // Convenience methods to send specific types of control frames:
  bool SendGoAwayFrame();
  bool SendRstStreamFrame(spdy::SpdyStreamId stream_id,
                          spdy::SpdyStatusCodes status);
  bool SendSettingsFrame();

  // Close down the whole session, post-haste.  Block until all stream
  // threads have shut down.
  void StopSession();
  // Abort the stream without sending anything to the client.
  void AbortStreamSilently(spdy::SpdyStreamId stream_id);
  // Send a RST_STREAM frame and then abort the stream.
  void AbortStream(spdy::SpdyStreamId stream_id, spdy::SpdyStatusCodes status);

  // Remove the given StreamTaskWrapper object from the stream map.  This is
  // the method of this class that might be called from another thread.
  // (Specifically, it is called by the StreamTaskWrapper destructor, which is
  // called by the executor).
  void RemoveStreamTask(StreamTaskWrapper* stream_data);

  // These fields are accessed only by the main connection thread, so they need
  // not be protected by a lock:
  const SpdyServerConfig* const config_;
  SpdySessionIO* const session_io_;
  SpdyStreamTaskFactory* const task_factory_;
  Executor* const executor_;
  spdy::SpdyFramer framer_;
  bool session_stopped_;
  bool already_sent_goaway_;
  spdy::SpdyStreamId last_client_stream_id_;

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
