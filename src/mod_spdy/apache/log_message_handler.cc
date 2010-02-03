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

#include "mod_spdy/apache/log_message_handler.h"

#include "base/logging.h"
#include "third_party/apache_httpd/include/httpd.h"
#include "third_party/apache_httpd/include/http_log.h"

namespace {

int GetApacheLogLevel(int severity) {
  switch (severity) {
    case logging::LOG_INFO:
      return APLOG_NOTICE;
    case logging::LOG_WARNING:
      return APLOG_WARNING;
    case logging::LOG_ERROR:
      return APLOG_ERR;
    case logging::LOG_ERROR_REPORT:
      return APLOG_CRIT;
    default:
      return APLOG_NOTICE;
  }  
}

bool LogMessageHandler(int severity, 
		       const std::string& str) {
  // Trim the newline off the end of the message string.
  std::string message = str;
  size_t last_msg_character_index = message.length() - 1;
  if (message[last_msg_character_index] == '\n') {
    message.resize(last_msg_character_index);
  }
  ap_log_perror(APLOG_MARK, 
		GetApacheLogLevel(severity),
		APR_SUCCESS, 
		NULL,
		"%s", message.c_str());
  return true;
}

}  // namespace

namespace mod_spdy {

void InstallLogMessageHandler() {
  logging::SetLogMessageHandler(&LogMessageHandler);
}

}  // namespace mod_spdy
