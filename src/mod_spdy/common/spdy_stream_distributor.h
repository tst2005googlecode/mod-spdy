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

#include "net/spdy/spdy_framer.h"

namespace mod_spdy {

class SpdyFramerVisitorFactoryInterface {
public:
  SpdyFramerVisitorFactoryInterface();
  virtual ~SpdyFramerVisitorFactoryInterface();

  virtual spdy::SpdyFramerVisitorInterface *Create(
      spdy::SpdyStreamId stream_id) = 0;
};

// The SpdyStreamDistributor is responsible for delivering all
// SpdyFramerVisitorInterface callbacks to a dedicated instance of
// SpdyFramerVisitorInterface associated with that stream.
class SpdyStreamDistributor : public spdy::SpdyFramerVisitorInterface {
 public:
  SpdyStreamDistributor(spdy::SpdyFramer *framer,
                        SpdyFramerVisitorFactoryInterface *factory);
  virtual ~SpdyStreamDistributor();

  virtual void OnError(spdy::SpdyFramer *framer);
  virtual void OnControl(const spdy::SpdyControlFrame *frame);
  virtual void OnStreamFrameData(spdy::SpdyStreamId stream_id,
                                 const char *data,
                                 size_t len);

  bool HasError() const { return error_; }

private:
  bool IsStreamControlFrame(const spdy::SpdyControlFrame *frame) const;

  void OnStreamControl(const spdy::SpdyControlFrame *frame);
  void OnSessionControl(const spdy::SpdyControlFrame *frame);

  spdy::SpdyFramerVisitorInterface *GetFramerForStreamId(spdy::SpdyStreamId id);

  typedef std::map<spdy::SpdyStreamId, spdy::SpdyFramerVisitorInterface *>
      StreamIdToVisitorMap;

  StreamIdToVisitorMap map_;
  spdy::SpdyFramer *const framer_;
  SpdyFramerVisitorFactoryInterface *const factory_;
  bool error_;
};

}  // namespace mod_spdy

#endif  // MOD_SPDY_SPDY_STREAM_DISTRIBUTOR_H_
