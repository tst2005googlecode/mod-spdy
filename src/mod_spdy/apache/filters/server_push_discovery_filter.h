// Copyright 2014 Google Inc.
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

#ifndef MOD_SPDY_APACHE_FILTERS_SERVER_PUSH_DISCOVERY_FILTER_H_
#define MOD_SPDY_APACHE_FILTERS_SERVER_PUSH_DISCOVERY_FILTER_H_

#include <string>

#include "apr_buckets.h"
#include "util_filter.h"

#include "mod_spdy/common/spdy_server_config.h"

namespace mod_spdy {

class ServerPushDiscoveryLearner;
class ServerPushDiscoverySessionPool;

void ServerPushDiscoveryFilter(
    ap_filter_t* filter,
    apr_bucket_brigade* input_brigade,
    ServerPushDiscoveryLearner* learner,
    ServerPushDiscoverySessionPool* session_pool,
    int spdy_version,
    bool send_debug_headers);


}  // namespace mod_spdy

#endif  // MOD_SPDY_APACHE_FILTERS_SERVER_PUSH_FILTER_H_
