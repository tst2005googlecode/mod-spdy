// Copyright 2010 Google Inc. All Rights Reserved.
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

#include "mod_spdy/common/spdy_session.h"

#include <list>
#include <string>

#include "base/basictypes.h"
#include "mod_spdy/common/spdy_server_config.h"
#include "mod_spdy/common/spdy_session_io.h"
#include "mod_spdy/common/spdy_stream_task_factory.h"
#include "mod_spdy/common/testing/spdy_frame_matchers.h"
#include "net/instaweb/util/public/function.h"
#include "net/spdy/spdy_framer.h"
#include "net/spdy/spdy_protocol.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using mod_spdy::FlagFinIs;
using mod_spdy::IsControlFrameOfType;
using mod_spdy::IsDataFrame;
using testing::_;
using testing::AllOf;
using testing::DoAll;
using testing::Eq;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::NotNull;
using testing::Property;
using testing::Return;
using testing::WithArg;

namespace {

void AddRequiredHeaders(spdy::SpdyHeaderBlock* headers) {
  (*headers)["host"] = "www.example.com";
  (*headers)["method"] = "GET";
  (*headers)["scheme"] = "https";
  (*headers)["url"] = "/foo/index.html";
  (*headers)["version"] = "HTTP/1.1";
}

class MockSpdySessionIO : public mod_spdy::SpdySessionIO {
 public:
  MOCK_METHOD0(IsConnectionAborted, bool());
  MOCK_METHOD2(ProcessAvailableInput, ReadStatus(bool, spdy::SpdyFramer*));
  MOCK_METHOD1(SendFrameRaw, WriteStatus(const spdy::SpdyFrame&));
};

class MockSpdyStreamTaskFactory : public mod_spdy::SpdyStreamTaskFactory {
 public:
  MOCK_METHOD1(NewStreamTask, net_instaweb::Function*(mod_spdy::SpdyStream*));
};

class FakeStreamTask : public net_instaweb::Function {
 public:
  virtual ~FakeStreamTask() {}
  static FakeStreamTask* SimpleResponse(mod_spdy::SpdyStream* stream) {
    return new FakeStreamTask(stream);
  }

 protected:
  // net_instaweb::Function methods:
  virtual void Run();
  virtual void Cancel() {}

 private:
  FakeStreamTask(mod_spdy::SpdyStream* stream) : stream_(stream) {}

  mod_spdy::SpdyStream* const stream_;
  spdy::SpdyFramer framer_;

  DISALLOW_COPY_AND_ASSIGN(FakeStreamTask);
};

void FakeStreamTask::Run() {
  if (!stream_->is_server_push()) {
    spdy::SpdyFrame* frame;
    ASSERT_TRUE(stream_->GetInputFrame(false, &frame));
    ASSERT_TRUE(frame != NULL);
    EXPECT_THAT(*frame, IsControlFrameOfType(spdy::SYN_STREAM));
  }

  spdy::SpdyHeaderBlock headers;
  headers["status"] = "200";
  headers["version"] = "HTTP/1.1";
  if (stream_->is_server_push()) {
    stream_->SendOutputFrame(framer_.CreateSynStream(
        stream_->stream_id(),
        stream_->associated_stream_id(),
        stream_->priority(),
        spdy::CONTROL_FLAG_NONE,
        false,  // false = don't use compression
        &headers));
  } else {
    stream_->SendOutputFrame(framer_.CreateSynReply(
        stream_->stream_id(),
        spdy::CONTROL_FLAG_NONE,
        false,  // false = don't use compression
        &headers));
  }

  stream_->SendOutputFrame(framer_.CreateDataFrame(
      stream_->stream_id(), "foobar", 6, spdy::DATA_FLAG_NONE));
  stream_->SendOutputFrame(framer_.CreateDataFrame(
      stream_->stream_id(), "quux", 4, spdy::DATA_FLAG_FIN));
}

// An executor that runs all tasks in the same thread, either immediately when
// they are added or when it is told to run them.
class InlineExecutor : public mod_spdy::Executor {
 public:
  InlineExecutor() : run_on_add_(false), stopped_(false) {}
  virtual ~InlineExecutor() { Stop(); }

  virtual void AddTask(net_instaweb::Function* task,
                       spdy::SpdyPriority priority) {
    if (stopped_) {
      task->CallCancel();
    } else if (run_on_add_) {
      task->CallRun();
    } else {
      tasks_.push_back(task);
    }
  }
  virtual void Stop() {
    stopped_ = true;
    while (!tasks_.empty()) {
      tasks_.front()->CallCancel();
      tasks_.pop_front();
    }
  }
  void RunOne() {
    if (!tasks_.empty()) {
      tasks_.front()->CallRun();
      tasks_.pop_front();
    }
  }
  void RunAll() {
    while (!tasks_.empty()) {
      RunOne();
    }
  }
  void set_run_on_add(bool run) { run_on_add_ = run; }
  bool stopped() const { return stopped_; }

 private:
  std::list<net_instaweb::Function*> tasks_;
  bool run_on_add_;
  bool stopped_;

  DISALLOW_COPY_AND_ASSIGN(InlineExecutor);
};

class SpdySessionTest : public testing::Test {
 public:
  SpdySessionTest()
      : session_(&config_, &session_io_, &task_factory_, &executor_) {
    ON_CALL(session_io_, IsConnectionAborted()).WillByDefault(Return(false));
    ON_CALL(session_io_, ProcessAvailableInput(_, NotNull()))
        .WillByDefault(Invoke(this, &SpdySessionTest::ReadNextInputChunk));
    ON_CALL(session_io_, SendFrameRaw(_))
        .WillByDefault(Return(mod_spdy::SpdySessionIO::WRITE_SUCCESS));
  }

  // Use as gMock action for ProcessAvailableInput:
  //   Invoke(this, &SpdySessionTest::ReadNextInputChunk)
  mod_spdy::SpdySessionIO::ReadStatus ReadNextInputChunk(
      bool block, spdy::SpdyFramer* framer) {
    if (input_queue_.empty()) {
      return mod_spdy::SpdySessionIO::READ_CONNECTION_CLOSED;
    }
    const std::string chunk = input_queue_.front();
    input_queue_.pop_front();
    framer->ProcessInput(chunk.data(), chunk.size());
    return (framer->HasError() ? mod_spdy::SpdySessionIO::READ_ERROR :
            mod_spdy::SpdySessionIO::READ_SUCCESS);
  }

 protected:
  // Push some random garbage bytes into the input queue.
  void PushGarbageData() {
    input_queue_.push_back("\x88\x5f\x92\x02\xf8\x92\x12\xd1"
                           "\x82\xdc\x1a\x40\xbb\xb2\x9d\x13");
  }

  // Push a frame into the input queue.
  void PushFrame(const spdy::SpdyFrame& frame) {
    input_queue_.push_back(std::string(
        frame.data(), frame.length() + spdy::SpdyFrame::size()));
  }

  // Push a PING frame into the input queue.
  void PushPingFrame(unsigned char id) {
    // TODO(mdsteele): Sadly, the version of SpdyFramer we're currently using
    // doesn't provide a method for creating PING frames.  So for now, we'll
    // create one manually here.
    const char data[] = {
      0x80, 0x02, 0x00, 0x06,  // SPDY v2, frame type = 6
      0x00, 0x00, 0x00, 0x04,  // flags = 0, frame length = 4
      0x00, 0x00, 0x00,   id   // ping ID
    };
    input_queue_.push_back(std::string(data, arraysize(data)));
  }

  // Push a valid SYN_STREAM frame into the input queue.
  void PushSynStreamFrame(spdy::SpdyStreamId stream_id,
                          spdy::SpdyPriority priority,
                          spdy::SpdyControlFlags flags) {
    spdy::SpdyHeaderBlock headers;
    AddRequiredHeaders(&headers);
    scoped_ptr<spdy::SpdySynStreamControlFrame> frame(framer_.CreateSynStream(
        stream_id, 0, priority, flags,
        true,  // true = use compression
        &headers));
    PushFrame(*frame);
  }

  spdy::SpdyFramer framer_;
  mod_spdy::SpdyServerConfig config_;
  MockSpdySessionIO session_io_;
  MockSpdyStreamTaskFactory task_factory_;
  InlineExecutor executor_;
  mod_spdy::SpdySession session_;
  std::list<std::string> input_queue_;
};

// Test that if the connection is already closed, we stop immediately.
TEST_F(SpdySessionTest, ConnectionAlreadyClosed) {
  testing::InSequence seq;
  EXPECT_CALL(session_io_, SendFrameRaw(IsControlFrameOfType(spdy::SETTINGS)))
      .WillOnce(Return(mod_spdy::SpdySessionIO::WRITE_CONNECTION_CLOSED));

  session_.Run();
  EXPECT_TRUE(executor_.stopped());
}

// Test that when the connection is aborted, we stop.
TEST_F(SpdySessionTest, ImmediateConnectionAbort) {
  testing::InSequence seq;
  EXPECT_CALL(session_io_, SendFrameRaw(IsControlFrameOfType(spdy::SETTINGS)));
  EXPECT_CALL(session_io_, IsConnectionAborted()).WillOnce(Return(true));

  session_.Run();
  EXPECT_TRUE(executor_.stopped());
}

// Test responding to a PING frame from the client (followed by the connection
// closing, so that we can exit the Run loop).
TEST_F(SpdySessionTest, SinglePing) {
  PushPingFrame(1);

  testing::InSequence seq;
  EXPECT_CALL(session_io_, SendFrameRaw(IsControlFrameOfType(spdy::SETTINGS)));
  EXPECT_CALL(session_io_, IsConnectionAborted());
  EXPECT_CALL(session_io_, ProcessAvailableInput(Eq(true), NotNull()));
  EXPECT_CALL(session_io_, SendFrameRaw(IsControlFrameOfType(spdy::PING)));
  EXPECT_CALL(session_io_, IsConnectionAborted());
  EXPECT_CALL(session_io_, ProcessAvailableInput(Eq(true), NotNull()));

  session_.Run();
  EXPECT_TRUE(executor_.stopped());
}

// Test handling a single stream request.
TEST_F(SpdySessionTest, SingleStream) {
  executor_.set_run_on_add(true);
  const spdy::SpdyStreamId stream_id = 1;
  const spdy::SpdyPriority priority = 2;
  PushSynStreamFrame(stream_id, priority, spdy::CONTROL_FLAG_FIN);

  testing::InSequence seq;
  EXPECT_CALL(session_io_, SendFrameRaw(IsControlFrameOfType(spdy::SETTINGS)));
  EXPECT_CALL(session_io_, IsConnectionAborted());
  EXPECT_CALL(session_io_, ProcessAvailableInput(Eq(true), NotNull()));
  EXPECT_CALL(task_factory_, NewStreamTask(
      AllOf(Property(&mod_spdy::SpdyStream::stream_id, Eq(stream_id)),
            Property(&mod_spdy::SpdyStream::associated_stream_id, Eq(0)),
            Property(&mod_spdy::SpdyStream::priority, Eq(priority)))))
      .WillOnce(WithArg<0>(Invoke(FakeStreamTask::SimpleResponse)));
  EXPECT_CALL(session_io_, SendFrameRaw(
      AllOf(IsControlFrameOfType(spdy::SYN_REPLY), FlagFinIs(false))));
  EXPECT_CALL(session_io_, SendFrameRaw(
      AllOf(IsDataFrame(), FlagFinIs(false))));
  EXPECT_CALL(session_io_, SendFrameRaw(
      AllOf(IsDataFrame(), FlagFinIs(true))));
  EXPECT_CALL(session_io_, IsConnectionAborted());
  EXPECT_CALL(session_io_, ProcessAvailableInput(Eq(true), NotNull()));

  session_.Run();
  EXPECT_TRUE(executor_.stopped());
}

// Test that if SendFrameRaw fails, we immediately stop trying to send data and
// shut down the session.
TEST_F(SpdySessionTest, ShutDownSessionIfSendFrameRawFails) {
  executor_.set_run_on_add(true);
  PushSynStreamFrame(1, 2, spdy::CONTROL_FLAG_FIN);

  testing::InSequence seq;
  // We start out the same way as in the SingleStream test above.
  EXPECT_CALL(session_io_, SendFrameRaw(IsControlFrameOfType(spdy::SETTINGS)));
  EXPECT_CALL(session_io_, IsConnectionAborted());
  EXPECT_CALL(session_io_, ProcessAvailableInput(_, _));
  EXPECT_CALL(task_factory_, NewStreamTask(_))
      .WillOnce(WithArg<0>(Invoke(FakeStreamTask::SimpleResponse)));
  EXPECT_CALL(session_io_, SendFrameRaw(
      AllOf(IsControlFrameOfType(spdy::SYN_REPLY), FlagFinIs(false))));
  // At this point, the connection is closed by the client.
  EXPECT_CALL(session_io_, SendFrameRaw(
      AllOf(IsDataFrame(), FlagFinIs(false))))
      .WillOnce(Return(mod_spdy::SpdySessionIO::WRITE_CONNECTION_CLOSED));
  // Even though we have another frame to send at this point (already in the
  // output queue), we immediately stop sending data and exit the session.

  session_.Run();
  EXPECT_TRUE(executor_.stopped());
}

// Test that when the client sends us garbage data, we send a GOAWAY frame and
// then quit.
TEST_F(SpdySessionTest, SendGoawayInResponseToGarbage) {
  PushGarbageData();

  testing::InSequence seq;
  EXPECT_CALL(session_io_, SendFrameRaw(IsControlFrameOfType(spdy::SETTINGS)));
  EXPECT_CALL(session_io_, IsConnectionAborted());
  EXPECT_CALL(session_io_, ProcessAvailableInput(Eq(true), NotNull()));
  EXPECT_CALL(session_io_, SendFrameRaw(IsControlFrameOfType(spdy::GOAWAY)));

  session_.Run();
  EXPECT_TRUE(executor_.stopped());
}

// Test that when the client sends us a SYN_STREAM with a corrupted header
// block, we send a GOAWAY frame and then quit.
TEST_F(SpdySessionTest, SendGoawayForBadSynStreamCompression) {
  spdy::SpdyHeaderBlock headers;
  headers["foobar"] = "Foo is to bar as bar is to baz.";
  scoped_ptr<spdy::SpdyFrame> frame(framer_.CreateSynStream(
      1, 0, SPDY_PRIORITY_HIGHEST, spdy::CONTROL_FLAG_FIN,
      false,  // false = no compression
      &headers));
  PushFrame(*frame);

  testing::InSequence seq;
  EXPECT_CALL(session_io_, SendFrameRaw(IsControlFrameOfType(spdy::SETTINGS)));
  EXPECT_CALL(session_io_, IsConnectionAborted());
  EXPECT_CALL(session_io_, ProcessAvailableInput(Eq(true), NotNull()));
  EXPECT_CALL(session_io_, SendFrameRaw(IsControlFrameOfType(spdy::GOAWAY)));

  session_.Run();
  EXPECT_TRUE(executor_.stopped());
}

// Test that when the client sends us a SYN_STREAM with a stream ID of 0, we
// send a GOAWAY frame and then quit.
TEST_F(SpdySessionTest, SendGoawayForSynStreamIdZero) {
  spdy::SpdyHeaderBlock headers;
  AddRequiredHeaders(&headers);
  // SpdyFramer DCHECKS that the stream_id isn't zero, so just create the frame
  // with a stream_id of 1, and then set the stream_id on the next line.
  scoped_ptr<spdy::SpdySynStreamControlFrame> frame(framer_.CreateSynStream(
      1, 0, SPDY_PRIORITY_HIGHEST, spdy::CONTROL_FLAG_FIN, true, &headers));
  frame->set_stream_id(0);
  PushFrame(*frame);

  testing::InSequence seq;
  EXPECT_CALL(session_io_, SendFrameRaw(IsControlFrameOfType(spdy::SETTINGS)));
  EXPECT_CALL(session_io_, IsConnectionAborted());
  EXPECT_CALL(session_io_, ProcessAvailableInput(Eq(true), NotNull()));
  EXPECT_CALL(session_io_, SendFrameRaw(IsControlFrameOfType(spdy::GOAWAY)));

  session_.Run();
  EXPECT_TRUE(executor_.stopped());
}

// Test that when the client sends us a SYN_STREAM with invalid flags, we
// send a GOAWAY frame and then quit.
TEST_F(SpdySessionTest, SendGoawayForSynStreamWithInvalidFlags) {
  spdy::SpdyHeaderBlock headers;
  AddRequiredHeaders(&headers);
  // SpdyFramer DCHECKS that the flags are valid, so just create the frame
  // with no flags, and then set the flags on the next line.
  scoped_ptr<spdy::SpdySynStreamControlFrame> frame(framer_.CreateSynStream(
      1, 0, SPDY_PRIORITY_HIGHEST, spdy::CONTROL_FLAG_NONE, true, &headers));
  frame->set_flags(0x47);
  PushFrame(*frame);

  testing::InSequence seq;
  EXPECT_CALL(session_io_, SendFrameRaw(IsControlFrameOfType(spdy::SETTINGS)));
  EXPECT_CALL(session_io_, IsConnectionAborted());
  EXPECT_CALL(session_io_, ProcessAvailableInput(Eq(true), NotNull()));
  EXPECT_CALL(session_io_, SendFrameRaw(IsControlFrameOfType(spdy::GOAWAY)));

  session_.Run();
  EXPECT_TRUE(executor_.stopped());
}

// Test that when the client sends us two SYN_STREAMs with the same ID, we send
// a GOAWAY frame (but still finish out the good stream before quitting).
TEST_F(SpdySessionTest, SendGoawayForDuplicateStreamId) {
  executor_.set_run_on_add(false);
  const spdy::SpdyStreamId stream_id = 1;
  const spdy::SpdyPriority priority = 2;
  PushSynStreamFrame(stream_id, priority, spdy::CONTROL_FLAG_FIN);
  PushSynStreamFrame(stream_id, priority, spdy::CONTROL_FLAG_FIN);

  testing::InSequence seq;
  EXPECT_CALL(session_io_, SendFrameRaw(IsControlFrameOfType(spdy::SETTINGS)));
  EXPECT_CALL(session_io_, IsConnectionAborted());
  // Get the first SYN_STREAM; it looks good, so create a new task (but because
  // we set executor_.set_run_on_add(false) above, it doesn't execute yet).
  EXPECT_CALL(session_io_, ProcessAvailableInput(Eq(true), NotNull()));
  EXPECT_CALL(task_factory_, NewStreamTask(
      AllOf(Property(&mod_spdy::SpdyStream::stream_id, Eq(stream_id)),
            Property(&mod_spdy::SpdyStream::associated_stream_id, Eq(0)),
            Property(&mod_spdy::SpdyStream::priority, Eq(priority)))))
      .WillOnce(WithArg<0>(Invoke(FakeStreamTask::SimpleResponse)));
  EXPECT_CALL(session_io_, IsConnectionAborted());
  // There's an active stream out, so ProcessAvailableInput should have false
  // for the first argument (false = nonblocking read).  Here we get the second
  // SYN_STREAM with the same stream ID, so we should send GOAWAY.
  EXPECT_CALL(session_io_, ProcessAvailableInput(Eq(false), NotNull()));
  EXPECT_CALL(session_io_, SendFrameRaw(IsControlFrameOfType(spdy::GOAWAY)));
  // At this point, tell the executor to run the task.
  EXPECT_CALL(session_io_, IsConnectionAborted())
      .WillOnce(DoAll(InvokeWithoutArgs(&executor_, &InlineExecutor::RunAll),
                      Return(false)));
  // The stream is no longer active, but there are pending frames to send, so
  // we shouldn't block on input.
  EXPECT_CALL(session_io_, ProcessAvailableInput(Eq(false), NotNull()));
  // Now we should send the output.
  EXPECT_CALL(session_io_, SendFrameRaw(
      AllOf(IsControlFrameOfType(spdy::SYN_REPLY), FlagFinIs(false))));
  EXPECT_CALL(session_io_, SendFrameRaw(
      AllOf(IsDataFrame(), FlagFinIs(false))));
  EXPECT_CALL(session_io_, SendFrameRaw(
      AllOf(IsDataFrame(), FlagFinIs(true))));
  // Finally, there is no more input to read and no more output to send, so we
  // quit.
  EXPECT_CALL(session_io_, IsConnectionAborted());

  session_.Run();
  EXPECT_TRUE(executor_.stopped());
}

}  // namespace
