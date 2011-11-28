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

const char* SetSpdyEnabled(cmd_parms* cmd, void* dir, const char* arg) {
  if (0 == apr_strnatcasecmp(arg, "on")) {
    GetServerConfig(cmd)->set_spdy_enabled(true);
    return NULL;
  } else if (0 == apr_strnatcasecmp(arg, "off")) {
    GetServerConfig(cmd)->set_spdy_enabled(false);
    return NULL;
  } else {
    return "SpdyEnabled on|off";
  }
}

const char* SetMaxStreamsPerConnection(cmd_parms* cmd, void* dir,
                                       const char* arg) {
  int value;
  if (!base::StringToInt(arg, &value) || value < 1) {
    return "SpdyMaxStreamsPerConnection must specify a positive integer";
  }
  GetServerConfig(cmd)->set_max_streams_per_connection(value);
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
      "SpdyEnabled", SetSpdyEnabled, "Enable SPDY support"),
  SPDY_CONFIG_COMMAND(
      "SpdyMaxStreamsPerConnection", SetMaxStreamsPerConnection,
      "Maxiumum number of simultaneous SPDY streams per connection"),
  {NULL}
};

}  // namespace mod_spdy
