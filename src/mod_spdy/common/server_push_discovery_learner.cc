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

namespace mod_spdy {

namespace {

typedef std::map<std::string, ServerPushDiscoveryAdjacentData> AdjacentDataMap;

bool EndsWith(const std::string& url, const std::string& ending) {
  if (ending.size() > url.size())
    return false;
  return url.substr(url.size() - ending.size()) == ending;
}

int32_t ExtensionGetPriority(const std::string& url) {
  if (EndsWith(url, ".js"))
    return 1;
  else if (EndsWith(url, ".css"))
    return 1;
  else
    return -1;
}

}  // namespace

ServerPushDiscoveryLearner::ServerPushDiscoveryLearner() {}

std::vector<ServerPushDiscoveryPush>
ServerPushDiscoveryLearner::GetPushes(const std::string& master_url) {
  base::AutoLock lock(lock_);
  ServerPushDiscoveryUrlData& url_data = url_data_[master_url];
  std::vector<ServerPushDiscoveryPush> pushes;

  uint64_t threshold = url_data.first_hit_count / 2;

  std::vector<ServerPushDiscoveryAdjacentData> significant_adjacents;

  for (AdjacentDataMap::const_iterator it = url_data.adjcaents.begin();
       it != url_data.adjcaents.end(); ++it) {
    if (it->second.hit_count >= threshold)
      significant_adjacents.push_back(it->second);
  }

  std::sort(significant_adjacents.begin(), significant_adjacents.end());

  for (size_t i = 0; i < significant_adjacents.size(); ++i) {
    const ServerPushDiscoveryAdjacentData& adjacent = significant_adjacents[i];
    int32_t priority = ExtensionGetPriority(adjacent.adjacent_url);

    if (priority < 0)
      priority = 2 + (i * 6 / significant_adjacents.size());

    //std::cout << "PushYes: P" << priority << " " << hit_data.hit_count << "/"
    //    << first_hits_ << " : " << hit_data.url << std::endl;
    pushes.push_back(ServerPushDiscoveryPush(adjacent.adjacent_url, priority));
  }

  return pushes;
}

void ServerPushDiscoveryLearner::AddFirstHit(const std::string& master_url) {
  base::AutoLock lock(lock_);
  ServerPushDiscoveryUrlData& url_data = url_data_[master_url];
  ++url_data.first_hit_count;
}

void ServerPushDiscoveryLearner::AddAdjacentHit(const std::string& master_url,
                                                const std::string& adjacent_url,
                                                int64_t time_from_init) {
  base::AutoLock lock(lock_);
  AdjacentDataMap& adjacents = url_data_[master_url].adjcaents;

  std::pair<AdjacentDataMap::iterator, bool> insertion = adjacents.insert(
      make_pair(adjacent_url, ServerPushDiscoveryAdjacentData(adjacent_url)));

  ServerPushDiscoveryAdjacentData& adjacent_data = insertion.first->second;

  ++adjacent_data.hit_count;
  double inv_new_total = 1.0 / adjacent_data.hit_count;

  adjacent_data.avg_time_from_init =
      inv_new_total * time_from_init +
      (1 - inv_new_total) * adjacent_data.avg_time_from_init;
}

}  // namespace mod_spdy
