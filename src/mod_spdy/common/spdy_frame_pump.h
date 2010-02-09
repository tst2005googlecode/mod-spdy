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

#ifndef MOD_SPDY_SPDY_FRAME_PUMP_H_
#define MOD_SPDY_SPDY_FRAME_PUMP_H_

#include <stddef.h>  // for size_t

#include "base/scoped_ptr.h"

namespace spdy {
class SpdyFramer;
}  // namespace spdy

namespace mod_spdy {

class InputStreamInterface;

// SpdyFramePump is an adapter that provides a pull interface to a
// SpdyFramer. Typically, SpdyFramer runs in push mode. Data is
// written into SpdyFramer as it becomes available, using
// SpdyFramer::ProcessInput().  SpdyFramePump provides a
// PumpOneFrame() method which attempts to pull a frame worth of data
// into the SpdyFramer. This is useful in environments that want to
// feed just enough data into the SpdyFramer to make progress, but no
// more.
class SpdyFramePump {
 public:
  SpdyFramePump(InputStreamInterface *input, spdy::SpdyFramer *framer);
  ~SpdyFramePump();

  // Pump a single spdy frame through the SpdyFramer.
  // @return true on success, false on failure (error, or partial frame).
  bool PumpOneFrame();

  bool HasError() const;

 private:
  bool PumpMoreBytes();
  bool PumpAtMost(size_t num_bytes);

  InputStreamInterface *const input_;
  spdy::SpdyFramer *const framer_;
  scoped_array<char> buf_;
  size_t frame_bytes_consumed_;
};

}  // namespace mod_spdy

#endif  // MOD_SPDY_SPDY_FRAME_PUMP_H_
