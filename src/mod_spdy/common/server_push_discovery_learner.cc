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

#include "mod_spdy/common/server_push_discovery_learner.h"

#include <algorithm>
#include <utility>

#include "base/strings/string_util.h"

namespace mod_spdy {

namespace {

int32_t GetPriorityFromExtension(const std::string& url) {
  if (EndsWith(url, ".js", false))
    return 1;
  else if (EndsWith(url, ".css", false))
    return 1;
  else
    return -1;
}

}  // namespace

ServerPushDiscoveryLearner::ServerPushDiscoveryLearner() {}

std::vector<ServerPushDiscoveryLearner::Push>
ServerPushDiscoveryLearner::GetPushes(const std::string& master_url) {
  base::AutoLock lock(lock_);
  UrlData& url_data = url_data_[master_url];
  std::vector<Push> pushes;

  uint64_t threshold = url_data.first_hit_count / 2;

  std::vector<AdjacentData> significant_adjacents;

  for (std::map<std::string, AdjacentData>::const_iterator it =
           url_data.adjcaents.begin(); it != url_data.adjcaents.end(); ++it) {
    if (it->second.hit_count >= threshold)
      significant_adjacents.push_back(it->second);
  }

  std::sort(significant_adjacents.begin(), significant_adjacents.end());

  for (size_t i = 0; i < significant_adjacents.size(); ++i) {
    const AdjacentData& adjacent = significant_adjacents[i];

    // Give certain URLs fixed high priorities based on their extension.
    int32_t priority = GetPriorityFromExtension(adjacent.adjacent_url);

    // Otherwise, assign a higher priority based on its average request order.
    if (priority < 0) {
      priority = 2 + (i * 6 / significant_adjacents.size());
    }

    pushes.push_back(Push(adjacent.adjacent_url, priority));
  }

  return pushes;
}

void ServerPushDiscoveryLearner::AddFirstHit(const std::string& master_url) {
  base::AutoLock lock(lock_);
  UrlData& url_data = url_data_[master_url];
  ++url_data.first_hit_count;
}

void ServerPushDiscoveryLearner::AddAdjacentHit(const std::string& master_url,
                                                const std::string& adjacent_url,
                                                int64_t time_from_init) {
  base::AutoLock lock(lock_);

  std::pair<std::map<std::string, AdjacentData>::iterator, bool> insertion =
      url_data_[master_url].adjcaents.insert(
          make_pair(adjacent_url, AdjacentData(adjacent_url)));

  AdjacentData& adjacent_data = insertion.first->second;

  ++adjacent_data.hit_count;
  double inv_new_total = 1.0 / adjacent_data.hit_count;

  adjacent_data.avg_time_from_init =
      inv_new_total * time_from_init +
      (1 - inv_new_total) * adjacent_data.avg_time_from_init;
}

}  // namespace mod_spdy
