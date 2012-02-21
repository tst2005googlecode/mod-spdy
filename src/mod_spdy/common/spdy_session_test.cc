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
using testing::Eq;
using testing::Invoke;
using testing::Return;

namespace {

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
      : session_(&config_, &session_io_, &task_factory_, &executor_) {}

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
    return mod_spdy::SpdySessionIO::READ_SUCCESS;
  }

 protected:
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

  mod_spdy::SpdyServerConfig config_;
  MockSpdySessionIO session_io_;
  MockSpdyStreamTaskFactory task_factory_;
  InlineExecutor executor_;
  mod_spdy::SpdySession session_;
  std::list<std::string> input_queue_;
};

// Define a gMock matcher that checks that a const spdy::SpdyFrame& is a
// control frame with the specified type.
MATCHER_P(IsControlFrameOfType, type, "") {
  return (arg.is_control_frame() &&
          static_cast<const spdy::SpdyControlFrame*>(&arg)->type() == type);
}

// Test that if the connection is already aborted, we stop immediately.
TEST_F(SpdySessionTest, ImmediateConnectionAbort) {
  EXPECT_CALL(session_io_, SendFrameRaw(IsControlFrameOfType(spdy::SETTINGS)))
      .WillOnce(Return(false));
  EXPECT_CALL(session_io_, IsConnectionAborted()).WillOnce(Return(true));

  session_.Run();
  EXPECT_TRUE(executor_.stopped());
}

// Test responding to a PING frame from the client (followed by the connection
// aborting, so that we can exit the Run loop).
TEST_F(SpdySessionTest, SinglePing) {
  testing::InSequence seq;
  PushPingFrame(1);
  EXPECT_CALL(session_io_, SendFrameRaw(IsControlFrameOfType(spdy::SETTINGS)))
      .WillOnce(Return(true));
  EXPECT_CALL(session_io_, IsConnectionAborted())
      .WillOnce(Return(false));
  EXPECT_CALL(session_io_, ProcessAvailableInput(Eq(true), _))
      .WillOnce(Invoke(this, &SpdySessionTest::ReadNextInputChunk));
  EXPECT_CALL(session_io_, SendFrameRaw(IsControlFrameOfType(spdy::PING)))
      .WillOnce(Return(true));
  EXPECT_CALL(session_io_, IsConnectionAborted())
      .WillOnce(Return(true));

  session_.Run();
  EXPECT_TRUE(executor_.stopped());
}

// TODO(mdsteele): Add more tests.

}  // namespace
