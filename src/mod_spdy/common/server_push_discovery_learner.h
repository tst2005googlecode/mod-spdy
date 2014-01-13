// Copyright 2013 Google Inc.
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

#ifndef MOD_SPDY_COMMON_SERVER_PUSH_DISCOVERY_LEARNER_H_
#define MOD_SPDY_COMMON_SERVER_PUSH_DISCOVERY_LEARNER_H_

#include <map>
#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/synchronization/lock.h"

namespace mod_spdy {

// Used to keep track of request patterns and generate X-Associated-Content.
// Stores the initial |master_url| request and the subsequent |adjacent_url|s.
// Generates reasonable pushes based on a simple heuristic.
class ServerPushDiscoveryLearner {
 public:
  struct Push {
    Push(const std::string& adjacent_url, int priority)
        : adjacent_url(adjacent_url),
          priority(priority) {
    }

    std::string adjacent_url;
    int priority;
  };

  ServerPushDiscoveryLearner();

  std::vector<Push> GetPushes(const std::string& master_url);

  void AddFirstHit(const std::string& master_url);

  void AddAdjacentHit(const std::string& master_url,
                      const std::string& adjacent_url, int64_t time_from_init);

 private:
  struct AdjacentData {
    AdjacentData(const std::string& adjacent_url)
        : adjacent_url(adjacent_url),
          hit_count(0),
          avg_time_from_init(0) {
    }

    std::string adjacent_url;
    uint64_t hit_count;
    int64_t avg_time_from_init;

    bool operator<(const AdjacentData& other) const {
      return avg_time_from_init < other.avg_time_from_init;
    }
  };

  struct UrlData {
    UrlData() : first_hit_count(0) {}

    uint64_t first_hit_count;
    std::map<std::string, AdjacentData> adjcaents;
  };

  std::map<std::string, UrlData> url_data_;
  base::Lock lock_;
};

}  // namespace mod_spdy

#endif  // MOD_SPDY_COMMON_SERVER_PUSH_DISCOVERY_LEARNER_H_
