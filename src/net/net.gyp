# Copyright (c) 2009 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'variables': {
    'chromium_code': 1,
  },
  'targets': [
    {
      'target_name': 'instaweb_util',
      'type': '<(library)',
      'dependencies': [
        '<(DEPTH)/base/base.gyp:base',
      ],
      'include_dirs': [
        '<(DEPTH)',
      ],
      'export_dependent_settings': [
        '<(DEPTH)/base/base.gyp:base',
      ],
      'sources': [
        # TODO(mdsteele): Add sources here as we need them.
        'instaweb/util/function.cc',
      ],
    },
    {
      'target_name': 'instaweb_test',
      'type': 'executable',
      'dependencies': [
        'instaweb_util',
        '<(DEPTH)/testing/gtest.gyp:gtest',
        '<(DEPTH)/testing/gtest.gyp:gtest_main',
      ],
      'include_dirs': [
        '<(DEPTH)',
      ],
      'sources': [
        # TODO(mdsteele): Add sources here as we need them.
        'instaweb/util/function_test.cc',
      ],
    },
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
        'spdy/spdy_bitmasks.h',
        'spdy/spdy_frame_builder.cc',
        'spdy/spdy_frame_builder.h',
        'spdy/spdy_framer.cc',
        'spdy/spdy_framer.h',
        'spdy/spdy_protocol.h',
      ],
    },
    {
      'target_name': 'spdy_test',
      'type': 'executable',
      'dependencies': [
        'spdy',
        '<(DEPTH)/testing/gmock.gyp:gmock',
        '<(DEPTH)/testing/gtest.gyp:gtest',
        '<(DEPTH)/testing/gtest.gyp:gtest_main',
      ],
      'include_dirs': [
        '<(DEPTH)',
      ],
      'sources': [
        'spdy/spdy_framer_test.cc',
        'spdy/spdy_protocol_test.cc',
      ],
    },
  ],
}
