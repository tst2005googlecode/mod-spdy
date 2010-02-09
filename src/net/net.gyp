# Copyright (c) 2009 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'variables': {
    'chromium_code': 1,
  },
  'targets': [
    {
      'target_name': 'spdy',
      'type': '<(library)',
      'dependencies': [
        '<(DEPTH)/base/base.gyp:base',
        '<(DEPTH)/third_party/zlib/zlib.gyp:zlib',
      ],
      'export_dependent_settings': [
        '<(DEPTH)/base/base.gyp:base',
      ],
      'include_dirs': [
        '<(DEPTH)',
      ],
      'sources': [
        'spdy/spdy_framer.cc',
        'spdy/spdy_frame_builder.cc',
      ],
    },
  ],
}
