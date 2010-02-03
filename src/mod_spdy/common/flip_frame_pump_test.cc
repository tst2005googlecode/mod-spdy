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

#include <algorithm>  // for std::min()

#include "base/scoped_ptr.h"
#include "mod_spdy/common/flip_frame_pump.h"
#include "mod_spdy/common/input_stream_interface.h"
#include "net/flip/flip_framer.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

const char kData[10000] = {0};

using testing::Eq;
using testing::Invoke;
using testing::InSequence;
using testing::NotNull;
using testing::Return;

class MockInputStream : public mod_spdy::InputStreamInterface {
 public:
  MockInputStream() : data_(NULL), data_len_(0), read_(0) {}

  MOCK_METHOD2(Read, size_t(char *, size_t));

  size_t DoRead(char *target, size_t target_len);

  // Helper used assert that the given flip::FlipFrame is equal to the
  // current input data.
  void AssertFrameEq(const flip::FlipFrame* frame);

  void set_data(const char *data, size_t data_len) {
    data_ = data;
    data_len_ = data_len;
    read_ = 0;
  }

  size_t data_available() const { return data_len_ - read_; }

  size_t bytes_read() const { return read_; }

 private:
  const char *data_;
  size_t data_len_;
  size_t read_;
};

class MockFlipFramerVisitor : public flip::FlipFramerVisitorInterface {
 public:
  MOCK_METHOD1(OnError, void(flip::FlipFramer*));
  MOCK_METHOD1(OnControl, void(const flip::FlipControlFrame*));
  MOCK_METHOD3(OnStreamFrameData,
               void(flip::FlipStreamId, const char*, size_t));
};

size_t MockInputStream::DoRead(char *target, size_t target_len) {
  size_t actual_len = std::min(target_len, data_len_ - read_);
  memcpy(target, data_ + read_, actual_len);
  read_ += actual_len;
  return actual_len;
}

void MockInputStream::AssertFrameEq(
    const flip::FlipFrame* frame) {
  ASSERT_EQ(flip::FlipFrame::size() + frame->length(), data_len_);
  ASSERT_EQ(0, memcmp(frame->data(), data_, data_len_));
}

TEST(FlipFramePumpTest, EmptyDataInputStream) {
  MockInputStream input;
  MockFlipFramerVisitor visitor;
  flip::FlipFramer consumer_framer;
  consumer_framer.set_visitor(&visitor);
  mod_spdy::FlipFramePump pump(&input, &consumer_framer);

  int num_attempts = 100;
  EXPECT_CALL(input,
              Read(NotNull(),
                   Eq(flip::FlipFrame::size())))
      .Times(100)
      .WillRepeatedly(Return(0));

  // Verify that the pump repeatedly refuses to pump data when no data
  // is available.
  for (int i = 0; i < 100; ++i) {
    ASSERT_FALSE(pump.PumpOneFrame());
  }

  ASSERT_FALSE(pump.HasError());
}

TEST(FlipFramePumpTest, OneSynFrame) {
  // Verify that the expected calls happen in sequence.
  InSequence seq;

  MockInputStream input;
  MockFlipFramerVisitor visitor;
  flip::FlipFramer consumer_framer;
  consumer_framer.set_visitor(&visitor);
  flip::FlipFramer generator_framer;
  mod_spdy::FlipFramePump pump(&input, &consumer_framer);

  flip::FlipHeaderBlock headers;
  scoped_ptr<flip::FlipSynStreamControlFrame> syn_stream_frame(
      generator_framer.CreateSynStream(
          1, 1, flip::CONTROL_FLAG_NONE, true, &headers));

  const size_t syn_frame_size =
      flip::FlipFrame::size() + syn_stream_frame->length();

  // Supply the frame's data to the input stream, so it can be pumped
  // through the FlipFramer.
  input.set_data(syn_stream_frame->data(), syn_frame_size);

  // We expect two calls to InputStreamInterface::Read(). The first
  // should read the FlipFrame header, and the second should read the
  // remaining bytes in the frame.
  EXPECT_CALL(input,
              Read(NotNull(),
                   Eq(flip::FlipFrame::size())))
      .WillOnce(Invoke(&input, &MockInputStream::DoRead));

  EXPECT_CALL(input,
              Read(NotNull(),
                   Eq(syn_stream_frame->length())))
      .WillOnce(Invoke(&input, &MockInputStream::DoRead));

  // Verify that the MockFlipFramerVisitor gets called back with the
  // expected frame.
  EXPECT_CALL(visitor, OnControl(NotNull()))
      .WillOnce(Invoke(&input, &MockInputStream::AssertFrameEq));

  ASSERT_EQ(0, input.bytes_read());
  ASSERT_EQ(syn_frame_size, input.data_available());

  ASSERT_TRUE(pump.PumpOneFrame());

  ASSERT_EQ(syn_frame_size, input.bytes_read());
  ASSERT_EQ(0, input.data_available());

  // Verify that the call to PumpOneFrame() triggered the expected
  // calls.
  testing::Mock::VerifyAndClearExpectations(&input);

  // Now verify that there is no additional data to read.
  EXPECT_CALL(input,
              Read(NotNull(),
                   Eq(flip::FlipFrame::size())))
      .WillOnce(Return(0));

  ASSERT_FALSE(pump.PumpOneFrame());

  ASSERT_FALSE(pump.HasError());
}

// Helper that computes the expected number of bytes the FlipFramePump
// will try to read, given the current offset and the size of the
// frame.
size_t ComputeExpectedReadLen(size_t offset, size_t syn_frame_size) {
  // If offset is less than 8, we're trying to read the header
  // block. Otherwise, we're trying to read to the end of the frame.
  if (offset < 8) {
    return flip::FlipFrame::size() - offset;
  } else {
    return syn_frame_size - offset;
  }
}

TEST(FlipFramePumpTest, OneSynFrameTrickle) {
  // Verify that the expected calls happen in sequence.
  InSequence seq;

  MockInputStream input;
  MockFlipFramerVisitor visitor;
  flip::FlipFramer consumer_framer;
  consumer_framer.set_visitor(&visitor);
  flip::FlipFramer generator_framer;
  mod_spdy::FlipFramePump pump(&input, &consumer_framer);

  flip::FlipHeaderBlock headers;
  scoped_ptr<flip::FlipSynStreamControlFrame> syn_stream_frame(
      generator_framer.CreateSynStream(
          1, 1, flip::CONTROL_FLAG_NONE, true, &headers));

  const size_t syn_frame_size =
      flip::FlipFrame::size() + syn_stream_frame->length();
  for (size_t offset = 0; offset < syn_frame_size - 1; ++offset) {
    // Supply the frame's data to the input stream, so it can be pumped
    // through the FlipFramer.
    input.set_data(syn_stream_frame->data() + offset, 1);

    size_t expected_read_len =
        ComputeExpectedReadLen(offset, syn_frame_size);

    EXPECT_CALL(input,
                Read(NotNull(),
                     Eq(expected_read_len)))
        .WillOnce(Invoke(&input, &MockInputStream::DoRead));

    if (offset == flip::FlipFrame::size() - 1) {
      // Special case: once the FlipFramePump consumes the header, it
      // determines that it can read the rest of the frame and
      // attempts to do so, so we expect an extra call to Read() in
      // this one case.
      EXPECT_CALL(input,
                  Read(NotNull(),
                       Eq(syn_stream_frame->length())))
          .WillOnce(Invoke(&input, &MockInputStream::DoRead));
    }

    ASSERT_EQ(0, input.bytes_read());
    ASSERT_EQ(1, input.data_available());

    ASSERT_FALSE(pump.PumpOneFrame());

    ASSERT_EQ(1, input.bytes_read());
    ASSERT_EQ(0, input.data_available());

    testing::Mock::VerifyAndClearExpectations(&input);

    // Now verify that we can attempt to read when there is no data
    // available.
    EXPECT_CALL(input,
                Read(NotNull(),
                     Eq(ComputeExpectedReadLen(offset + 1, syn_frame_size))))
        .WillOnce(Invoke(&input, &MockInputStream::DoRead));

    // Try to perform one additional read, to verify that we can
    // successfully attempt to read from an empty stream.
    ASSERT_FALSE(pump.PumpOneFrame());

    testing::Mock::VerifyAndClearExpectations(&input);
  }

  // Supply the final byte of the stream.
  input.set_data(syn_stream_frame->data() + (syn_frame_size - 1), 1);

  EXPECT_CALL(input,
              Read(NotNull(),
                   Eq(1)))
      .WillOnce(Invoke(&input, &MockInputStream::DoRead));

  // Verify that the MockFlipFramerVisitor gets called back with the
  // expected frame.
  EXPECT_CALL(visitor, OnControl(NotNull()));

  ASSERT_TRUE(pump.PumpOneFrame());

  // Now verify that there is no additional data to read.
  EXPECT_CALL(input,
              Read(NotNull(),
                   Eq(flip::FlipFrame::size())))
      .WillOnce(Return(0));

  ASSERT_FALSE(pump.PumpOneFrame());

  ASSERT_FALSE(pump.HasError());
}

TEST(FlipFramePumpTest, OneDataFrame) {
  // Verify that the expected calls happen in sequence.
  InSequence seq;

  MockInputStream input;
  MockFlipFramerVisitor visitor;
  flip::FlipFramer consumer_framer;
  consumer_framer.set_visitor(&visitor);
  flip::FlipFramer generator_framer;
  mod_spdy::FlipFramePump pump(&input, &consumer_framer);

  flip::FlipHeaderBlock headers;
  scoped_ptr<flip::FlipDataFrame> data_frame(
      generator_framer.CreateDataFrame(
          1, kData, sizeof(kData), flip::DATA_FLAG_NONE));

  const size_t data_frame_size =
      flip::FlipFrame::size() + data_frame->length();

  // Supply the frame's data to the input stream, so it can be pumped
  // through the FlipFramer.
  input.set_data(data_frame->data(), data_frame_size);

  // We expect two calls to InputStreamInterface::Read(). The first
  // should read the FlipFrame header, and the second should read the
  // remaining bytes in the frame.
  EXPECT_CALL(input,
              Read(NotNull(),
                   Eq(flip::FlipFrame::size())))
      .WillOnce(Invoke(&input, &MockInputStream::DoRead));

  EXPECT_CALL(input,
              Read(NotNull(),
                   Eq(4096)))
      .WillOnce(Invoke(&input, &MockInputStream::DoRead));
  EXPECT_CALL(visitor, OnStreamFrameData(Eq(1), NotNull(), Eq(4096)));

  EXPECT_CALL(input,
              Read(NotNull(),
                   Eq(4096)))
      .WillOnce(Invoke(&input, &MockInputStream::DoRead));
  EXPECT_CALL(visitor, OnStreamFrameData(Eq(1), NotNull(), Eq(4096)));

  EXPECT_CALL(input,
              Read(NotNull(),
                   Eq(1808)))
      .WillOnce(Invoke(&input, &MockInputStream::DoRead));
  EXPECT_CALL(visitor, OnStreamFrameData(Eq(1), NotNull(), Eq(1808)));

  ASSERT_EQ(0, input.bytes_read());
  ASSERT_EQ(data_frame_size, input.data_available());

  ASSERT_TRUE(pump.PumpOneFrame());

  ASSERT_EQ(data_frame_size, input.bytes_read());
  ASSERT_EQ(0, input.data_available());

  // Verify that the call to PumpOneFrame() triggered the expected
  // calls.
  testing::Mock::VerifyAndClearExpectations(&input);

  // Now verify that there is no additional data to read.
  EXPECT_CALL(input,
              Read(NotNull(),
                   Eq(flip::FlipFrame::size())))
      .WillOnce(Return(0));

  ASSERT_FALSE(pump.PumpOneFrame());

  ASSERT_FALSE(pump.HasError());
}

}  // namespace
