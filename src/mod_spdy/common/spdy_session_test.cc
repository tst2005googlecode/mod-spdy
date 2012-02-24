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
#include "net/instaweb/util/public/function.h"
#include "net/spdy/spdy_framer.h"
#include "net/spdy/spdy_protocol.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using testing::_;
using testing::AllOf;
using testing::Eq;
using testing::Invoke;
using testing::NotNull;
using testing::Property;
using testing::Return;
using testing::WithArg;

namespace {

// Define a gMock matcher that checks that a const spdy::SpdyFrame& is a
// control frame with the specified type.
MATCHER_P(IsControlFrameOfType, type, "") {
  if (!arg.is_control_frame()) {
    *result_listener << "is data frame for stream " <<
        static_cast<const spdy::SpdyDataFrame*>(&arg)->stream_id();
    return false;
  }
  const spdy::SpdyControlFrame* ctrl_frame =
      static_cast<const spdy::SpdyControlFrame*>(&arg);
  if (ctrl_frame->type() != type) {
    *result_listener << "is control frame of type " << ctrl_frame->type();
    return false;
  }
  return true;
}

// Define a gMock matcher that checks that a const spdy::SpdyFrame& is a
// data frame with the specified stream ID and FLAG_FIN value.
MATCHER_P2(IsDataFrame, stream_id, fin, "") {
  if (arg.is_control_frame()) {
    *result_listener << "is control frame of type " <<
        static_cast<const spdy::SpdyControlFrame*>(&arg)->type();
    return false;
  }
  const spdy::SpdyDataFrame* data_frame =
      static_cast<const spdy::SpdyDataFrame*>(&arg);
  if (data_frame->stream_id() != stream_id) {
    *result_listener << "is data frame for stream " << data_frame->stream_id();
    return false;
  }
  if (bool(data_frame->flags() & spdy::DATA_FLAG_FIN) != bool(fin)) {
    *result_listener << "is data frame with FLAG_FIN=" << !fin;
    return false;
  }
  return true;
}

class MockSpdySessionIO : public mod_spdy::SpdySessionIO {
 public:
  MOCK_METHOD0(IsConnectionAborted, bool());
  MOCK_METHOD2(ProcessAvailableInput, ReadStatus(bool, spdy::SpdyFramer*));
  MOCK_METHOD1(SendFrameRaw, bool(const spdy::SpdyFrame&));
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

// An executor that runs all tasks immediately when they are added.
class InlineExecutor : public mod_spdy::Executor {
 public:
  InlineExecutor() : stopped_(false) {}
  virtual ~InlineExecutor() {}

  virtual void AddTask(net_instaweb::Function* task,
                       spdy::SpdyPriority priority) {
    task->CallRun();
  }

  virtual void Stop() { stopped_ = true; }

  bool stopped() const { return stopped_; }

 private:
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
    ON_CALL(session_io_, SendFrameRaw(_)).WillByDefault(Return(true));
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

  // Push a SYN_STREAM frame into the input queue.
  void PushSynStreamFrame(spdy::SpdyStreamId stream_id,
                          spdy::SpdyPriority priority,
                          bool fin,
                          const spdy::SpdyHeaderBlock& headers) {
    scoped_ptr<spdy::SpdyFrame> frame(framer_.CreateSynStream(
        stream_id,
        0,  // associated_stream_id
        priority,
        (fin ? spdy::CONTROL_FLAG_FIN : spdy::CONTROL_FLAG_NONE),
        true,  // true = use compression
        const_cast<spdy::SpdyHeaderBlock*>(&headers)));
    input_queue_.push_back(std::string(
        frame->data(), frame->length() + spdy::SpdyFrame::size()));
  }

  // Push an improperly compressed SYN_STREAM frame into the input queue.
  void PushCorruptedSynStreamFrame(spdy::SpdyStreamId stream_id) {
    spdy::SpdyHeaderBlock headers;
    headers["foobar"] = "Foo is to bar as bar is to baz.";
    scoped_ptr<spdy::SpdyFrame> frame(framer_.CreateSynStream(
        stream_id, 0, SPDY_PRIORITY_HIGHEST, spdy::CONTROL_FLAG_FIN,
        false,  // false = no compression
        &headers));
    input_queue_.push_back(std::string(
        frame->data(), frame->length() + spdy::SpdyFrame::size()));
  }

  spdy::SpdyFramer framer_;
  mod_spdy::SpdyServerConfig config_;
  MockSpdySessionIO session_io_;
  MockSpdyStreamTaskFactory task_factory_;
  InlineExecutor executor_;
  mod_spdy::SpdySession session_;
  std::list<std::string> input_queue_;
};

// Test that if the connection is already aborted, we stop immediately.
TEST_F(SpdySessionTest, ImmediateConnectionAbort) {
  testing::InSequence seq;
  EXPECT_CALL(session_io_, SendFrameRaw(IsControlFrameOfType(spdy::SETTINGS)))
      .WillOnce(Return(false));
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
  const spdy::SpdyStreamId stream_id = 1;
  const spdy::SpdyPriority priority = 2;
  spdy::SpdyHeaderBlock headers;
  headers["host"] = "www.example.com";
  headers["method"] = "GET";
  headers["scheme"] = "https";
  headers["url"] = "/foo/index.html";
  headers["version"] = "HTTP/1.1";
  PushSynStreamFrame(stream_id, priority, true, headers);

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
      IsControlFrameOfType(spdy::SYN_REPLY)));
  EXPECT_CALL(session_io_, SendFrameRaw(IsDataFrame(stream_id, false)));
  EXPECT_CALL(session_io_, SendFrameRaw(IsDataFrame(stream_id, true)));
  EXPECT_CALL(session_io_, IsConnectionAborted());
  EXPECT_CALL(session_io_, ProcessAvailableInput(Eq(true), NotNull()));

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
  PushCorruptedSynStreamFrame(1);

  testing::InSequence seq;
  EXPECT_CALL(session_io_, SendFrameRaw(IsControlFrameOfType(spdy::SETTINGS)));
  EXPECT_CALL(session_io_, IsConnectionAborted());
  EXPECT_CALL(session_io_, ProcessAvailableInput(Eq(true), NotNull()));
  EXPECT_CALL(session_io_, SendFrameRaw(IsControlFrameOfType(spdy::GOAWAY)));

  session_.Run();
  EXPECT_TRUE(executor_.stopped());
}

}  // namespace
