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

#include "mod_spdy/apache/filters/server_push_discovery_filter.h"

#include <string>

#include "apr.h"
#include "apr_strings.h"
#include "base/logging.h"
#include "base/safe_numerics.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "mod_spdy/apache/slave_connection_context.h"
#include "mod_spdy/common/protocol_util.h"
#include "mod_spdy/common/server_push_discovery_learner.h"
#include "mod_spdy/common/server_push_discovery_session.h"
#include "util_cookies.h"

namespace mod_spdy {

namespace {

const char kServerPushDiscoveryCookieName[] = "ServerPushDiscoverySession";

}

void ServerPushDiscoveryFilter(
    ap_filter_t* filter,
    apr_bucket_brigade* input_brigade,
    ServerPushDiscoveryLearner* learner,
    ServerPushDiscoverySessionPool* session_pool,
    int spdy_version,
    bool send_debug_headers) {
  const char kServerPushDiscoveryCookieAttr[] = "Discard;Version=1";
  long kServerPushDiscoveryCookieMaxAge = 10;  // 10 seconds.

  // Exclude requests that already have the x-associated-content header set.
  if (apr_table_get(filter->r->headers_out, http::kXAssociatedContent)) {
    return;
  }

  apr_time_t request_time = filter->r->request_time;

  // Get server push discovery session from cookie.
  apr_int64_t session_id = 0;
  const char* cookie_val = NULL;
  apr_status_t status = util_cookies::ap_cookie_read(
      filter->r, kServerPushDiscoveryCookieName, &cookie_val, true);
  bool existing_session = false;
  if (APR_SUCCESS == status && cookie_val) {
    existing_session = base::StringToInt64(cookie_val, &session_id);
  }

  // If we have a pre-existing session, learn adjacent hits only.
  if (existing_session) {
    ServerPushDiscoverySession* session =
        session_pool->GetExistingSession(session_id, request_time);

    if (session && session->master_url() != std::string(filter->r->uri)) {
      session->UpdateLastAccessTime(request_time);

      if (!session->took_push()) {
        learner->AddAdjacentHit(session->master_url(), filter->r->uri,
                                session->TimeFromInit(request_time));
      }

      return;
    }
  }

  // Requests without existing sessions are treated as initial requests.
  bool takes_push = spdy_version >= spdy::SPDY_VERSION_3;

  // Set-up a push discovery session for the client using a short-lived cookie.
  int64_t new_session_id =
      session_pool->CreateSession(request_time, filter->r->uri, takes_push);
  apr_table_add(filter->r->headers_out, "Cache-Control",
                "no-cache=\"set-cookie\"");
  util_cookies::ap_cookie_write(filter->r, kServerPushDiscoveryCookieName,
                                base::Int64ToString(new_session_id).c_str(),
                                kServerPushDiscoveryCookieAttr,
                                kServerPushDiscoveryCookieMaxAge,
                                filter->r->headers_out,
                                NULL);

  // Write push instructions for client that can use it. Otherwise log an
  // initial hit.
  if (takes_push) {
    std::vector<ServerPushDiscoveryLearner::Push> pushes =
        learner->GetPushes(filter->r->uri);

    std::string push_content;
    for (std::vector<ServerPushDiscoveryLearner::Push>::iterator it =
             pushes.begin();
         it != pushes.end(); ++it) {
      base::StringAppendF(&push_content, "\"%s\":%d,", it->adjacent_url.c_str(),
                          base::checked_numeric_cast<int>(it->priority));
    }

    apr_table_add(filter->r->headers_out, http::kXAssociatedContent,
                  push_content.c_str());

    if (send_debug_headers) {
      apr_table_add(filter->r->headers_out, "x-associated-content-debug",
                     push_content.c_str());
    }
  } else {
    learner->AddFirstHit(filter->r->uri);
  }
}

}  // namespace mod_spdy
