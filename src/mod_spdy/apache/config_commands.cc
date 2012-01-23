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

#include "mod_spdy/apache/config_commands.h"

#include "apr_strings.h"

#include "base/string_number_conversions.h"

#include "mod_spdy/apache/config_util.h"
#include "mod_spdy/apache/pool_util.h"
#include "mod_spdy/common/spdy_server_config.h"

namespace mod_spdy {

void* CreateSpdyServerConfig(apr_pool_t* pool, server_rec* server) {
  SpdyServerConfig* config = new SpdyServerConfig;
  PoolRegisterDelete(pool, config);
  return config;
}

void* MergeSpdyServerConfigs(apr_pool_t* pool, void* base, void* add) {
  SpdyServerConfig* config = new SpdyServerConfig;
  PoolRegisterDelete(pool, config);
  config->MergeFrom(*static_cast<SpdyServerConfig*>(base),
                    *static_cast<SpdyServerConfig*>(add));
  return config;
}

namespace {

// A function suitable for for passing to AP_INIT_TAKE1 (and hence to
// SPDY_CONFIG_COMMAND) for a config option that requires a boolean argument
// ("on" or "off", case-insensitive; other strings will be rejected).  The
// template argument is a setter method on SpdyServerConfig that takes a bool.
template <void(SpdyServerConfig::*setter)(bool)>
const char* SetBoolean(cmd_parms* cmd, void* dir, const char* arg) {
  if (0 == apr_strnatcasecmp(arg, "on")) {
    (GetServerConfig(cmd)->*setter)(true);
    return NULL;
  } else if (0 == apr_strnatcasecmp(arg, "off")) {
    (GetServerConfig(cmd)->*setter)(false);
    return NULL;
  } else {
    return apr_pstrcat(cmd->pool, cmd->cmd->name, " on|off", NULL);
  }
}

// A function suitable for for passing to AP_INIT_TAKE1 (and hence to
// SPDY_CONFIG_COMMAND) for a config option that requires a positive integer
// argument.  The template argument is a setter method on SpdyServerConfig that
// takes an int; the method will only ever be called with a positive argument
// (if the user gives a non-positive argument, or a string that isn't even an
// integer, this function will reject it with an error message).
template <void(SpdyServerConfig::*setter)(int)>
const char* SetPositiveInt(cmd_parms* cmd, void* dir, const char* arg) {
  int value;
  if (!base::StringToInt(arg, &value) || value < 1) {
    return apr_pstrcat(cmd->pool, cmd->cmd->name,
                       " must specify a positive integer", NULL);
  }
  (GetServerConfig(cmd)->*setter)(value);
  return NULL;
}

// Like SetPositiveInt, but allows any non-negative value, not just positive.
template <void(SpdyServerConfig::*setter)(int)>
const char* SetNonNegativeInt(cmd_parms* cmd, void* dir, const char* arg) {
  int value;
  if (!base::StringToInt(arg, &value) || value < 0) {
    return apr_pstrcat(cmd->pool, cmd->cmd->name,
                       " must specify a non-negative integer", NULL);
  }
  (GetServerConfig(cmd)->*setter)(value);
  return NULL;
}

}  // namespace

// The reinterpret_cast is there because Apache's AP_INIT_TAKE1 macro needs to
// take an old-style C function type with unspecified arguments.  The
// static_cast, then, is just to enforce that we pass the correct type of
// function -- it will give a compile-time error if we pass a function with the
// wrong signature.
#define SPDY_CONFIG_COMMAND(name, fn, help)                               \
  AP_INIT_TAKE1(                                                          \
      name,                                                               \
      reinterpret_cast<const char*(*)()>(                                 \
          static_cast<const char*(*)(cmd_parms*,void*,const char*)>(fn)), \
      NULL, RSRC_CONF, help)

const command_rec kSpdyConfigCommands[] = {
  SPDY_CONFIG_COMMAND(
      "SpdyEnabled", SetBoolean<&SpdyServerConfig::set_spdy_enabled>,
      "Enable SPDY support"),
  SPDY_CONFIG_COMMAND(
      "SpdyMaxStreamsPerConnection",
      SetPositiveInt<&SpdyServerConfig::set_max_streams_per_connection>,
      "Maxiumum number of simultaneous SPDY streams per connection"),
  SPDY_CONFIG_COMMAND(
      "SpdyMaxThreadsPerProcess",
      SetPositiveInt<&SpdyServerConfig::set_max_threads_per_process>,
      "Maxiumum number of worker threads to spawn per child process"),
  // Debugging commands, which should not be used in production:
  SPDY_CONFIG_COMMAND(
      "SpdyDebugLoggingVerbosity",
      SetNonNegativeInt<&SpdyServerConfig::set_vlog_level>,
      "Set the verbosity of mod_spdy logging"),
  SPDY_CONFIG_COMMAND(
      "SpdyDebugUseSpdyForNonSslConnections",
      SetBoolean<&SpdyServerConfig::set_use_even_without_ssl>,
      "Use SPDY even over non-SSL connections; DO NOT USE IN PRODUCTION"),
  {NULL}
};

}  // namespace mod_spdy
