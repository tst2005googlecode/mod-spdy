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

#ifndef MOD_SPDY_SPDY_STREAM_DISTRIBUTOR_H_
#define MOD_SPDY_SPDY_STREAM_DISTRIBUTOR_H_

#include "net/flip/flip_framer.h"

namespace mod_spdy {

class FlipFramerVisitorFactoryInterface {
public:
  FlipFramerVisitorFactoryInterface();
  virtual ~FlipFramerVisitorFactoryInterface();

  virtual flip::FlipFramerVisitorInterface *Create(
      flip::FlipStreamId stream_id) = 0;
};

// The SpdyStreamDistributor is responsible for delivering all
// FlipFramerVisitorInterface callbacks to a dedicated instance of
// FlipFramerVisitorInterface associated with that stream.
class SpdyStreamDistributor : public flip::FlipFramerVisitorInterface {
 public:
  SpdyStreamDistributor(flip::FlipFramer *framer,
                        FlipFramerVisitorFactoryInterface *factory);
  virtual ~SpdyStreamDistributor();

  virtual void OnError(flip::FlipFramer *framer);
  virtual void OnControl(const flip::FlipControlFrame *frame);
  virtual void OnStreamFrameData(flip::FlipStreamId stream_id,
                                 const char *data,
                                 size_t len);

private:
  flip::FlipFramerVisitorInterface *GetFramerForStreamId(flip::FlipStreamId id);

  typedef std::map<flip::FlipStreamId, flip::FlipFramerVisitorInterface *>
      StreamIdToVisitorMap;

  StreamIdToVisitorMap map_;
  flip::FlipFramer *const framer_;
  FlipFramerVisitorFactoryInterface *const factory_;
};

}  // namespace mod_spdy

#endif  // MOD_SPDY_SPDY_STREAM_DISTRIBUTOR_H_
