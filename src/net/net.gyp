# Copyright (c) 2009 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'variables': {
    'chromium_code': 1,
  },
  'targets': [
    {
      'target_name': 'flip',
      'type': '<(library)',
      'dependencies': [
        '<(DEPTH)/base/base.gyp:base',
      ],
      'export_dependent_settings': [
        '<(DEPTH)/base/base.gyp:base',
      ],
      'include_dirs': [
        '<(DEPTH)',
      ],
      'sources': [
        'flip/flip_framer.cc',
        'flip/flip_frame_builder.cc',
      ],
    },
    {
      'target_name': 'flip_server_lib',
      'type': '<(library)',
      'dependencies': [
        '<(DEPTH)/base/base.gyp:base',
      ],
      'export_dependent_settings': [
        '<(DEPTH)/base/base.gyp:base',
      ],
      'include_dirs': [
        '<(DEPTH)',
      ],
      'sources': [
        'tools/flip_server/balsa_frame.cc',
        'tools/flip_server/balsa_headers.cc',
        'tools/flip_server/http_message_constants.cc',
        'tools/flip_server/simple_buffer.cc',
        'tools/flip_server/split.cc',
      ],
    },
  ],
}
