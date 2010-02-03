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

#ifndef MOD_SPDY_FLIP_FRAME_PUMP_H_
#define MOD_SPDY_FLIP_FRAME_PUMP_H_

#include <stddef.h>  // for size_t

#include "base/scoped_ptr.h"

namespace flip {
class FlipFramer;
}  // namespace flip

namespace mod_spdy {

class InputStreamInterface;

// FlipFramePump is an adapter that provides a pull interface to a
// FlipFramer. Typically, FlipFramer runs in push mode. Data is
// written into FlipFramer as it becomes available, using
// FlipFramer::ProcessInput().  FlipFramePump provides a
// PumpOneFrame() method which attempts to pull a frame worth of data
// into the FlipFramer. This is useful in environments that want to
// feed just enough data into the FlipFramer to make progress, but no
// more.
class FlipFramePump {
 public:
  FlipFramePump(InputStreamInterface *input, flip::FlipFramer *framer);
  ~FlipFramePump();

  // Pump a single flip frame through the FlipFramer.
  // @return true on success, false on failure (error, or partial frame).
  bool PumpOneFrame();

  bool HasError() const;

 private:
  bool PumpMoreBytes();
  bool PumpAtMost(size_t num_bytes);

  InputStreamInterface *const input_;
  flip::FlipFramer *const framer_;
  scoped_array<char> buf_;
  size_t frame_bytes_consumed_;
};

}  // namespace mod_spdy

#endif  // MOD_SPDY_FLIP_FRAME_PUMP_H_
