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

#ifndef MOD_SPDY_COMMON_SPDY_SERVER_CONFIG_H_
#define MOD_SPDY_COMMON_SPDY_SERVER_CONFIG_H_

#include "base/basictypes.h"

namespace mod_spdy {

// Stores server configuration settings for our module.
class SpdyServerConfig {
 public:
  SpdyServerConfig();
  ~SpdyServerConfig();

  // Return true if SPDY is enabled for this server, false otherwise.
  bool spdy_enabled() const { return spdy_enabled_.get(); }

  // Return the maximum number of simultaneous SPDY streams that should be
  // permitted for a single client connection.
  int max_streams_per_connection() const {
    return max_streams_per_connection_.get();
  }

  // Setters.  Call only during the configuration phase.
  void set_spdy_enabled(bool b) { spdy_enabled_.set(b); }
  void set_max_streams_per_connection(int n) {
    max_streams_per_connection_.set(n);
  }

  // Set this config object to the merge of a and b.  Call only during the
  // configuration phase.
  void MergeFrom(const SpdyServerConfig& a, const SpdyServerConfig& b);

 private:
  template <typename T>
  class Option {
   public:
    explicit Option(const T& default_value)
        : was_set_(false), value_(default_value) {}
    const T& get() const { return value_; }
    void set(const T& value) { was_set_ = true; value_ = value; }
    void MergeFrom(const Option<T>& a, const Option<T>& b) {
      was_set_ = a.was_set_ || b.was_set_;
      value_ = a.was_set_ ? a.value_ : b.value_;
    }
   private:
    bool was_set_;
    T value_;
    DISALLOW_COPY_AND_ASSIGN(Option);
  };

  // Configuration fields:
  Option<bool> spdy_enabled_;
  Option<int> max_streams_per_connection_;
  // Note: Add more config options here as needed; be sure to also update the
  //   MergeFrom method in spdy_server_config.cc.

  DISALLOW_COPY_AND_ASSIGN(SpdyServerConfig);
};

}  // namespace mod_spdy

#endif  // MOD_SPDY_CONTEXT_SPDY_SERVER_CONFIG_H_
