# Copyright 2010 Google Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

{
  'targets': [
    {
      'target_name': 'spdy_common',
      'type': '<(library)',
      'dependencies': [
        '<(DEPTH)/base/base.gyp:base',
        '<(DEPTH)/net/net.gyp:flip',
      ],
      'include_dirs': [
        '<(DEPTH)',
      ],
      'export_dependent_settings': [
        '<(DEPTH)/base/base.gyp:base',
        '<(DEPTH)/net/net.gyp:flip',
      ],
      'sources': [
        'common/connection_context.cc',
        'common/flip_frame_pump.cc',
        'common/http_stream_visitor_interface.cc',
        'common/input_stream_interface.cc',
        'common/output_filter_context.cc',
        'common/spdy_to_http_converter.cc',
      ],
    },
    {
      'target_name': 'spdy_apache',
      'type': '<(library)',
      'dependencies': [
        'spdy_common',
        '<(DEPTH)/base/base.gyp:base',
        '<(DEPTH)/third_party/apache_httpd/apache_httpd.gyp:apache_httpd',
      ],
      'include_dirs': [
        '<(DEPTH)',
      ],
      'export_dependent_settings': [
        'spdy_common',
        '<(DEPTH)/third_party/apache_httpd/apache_httpd.gyp:apache_httpd',
      ],
      'sources': [
        'apache/http_stream_accumulator.cc',
        'apache/input_filter_input_stream.cc',
      ],
    },
    {
      'target_name': 'mod_spdy',
      'type': 'loadable_module',
      'dependencies': [
        '<(DEPTH)/base/base.gyp:base',
        '<(DEPTH)/net/net.gyp:flip',
        '<(DEPTH)/third_party/apache_httpd/apache_httpd.gyp:apache_httpd',
      ],
      'include_dirs': [
        '<(DEPTH)',
      ],
      'sources': [
        'mod_spdy.cc',
      ],
    },
    {
      'target_name': 'spdy_common_test',
      'type': 'executable',
      'dependencies': [
        'spdy_common',
        '<(DEPTH)/testing/gmock.gyp:gmock',
        '<(DEPTH)/testing/gtest.gyp:gtest',
        '<(DEPTH)/testing/gtest.gyp:gtestmain',
      ],
      'include_dirs': [
        '<(DEPTH)',
      ],
      'sources': [
        'common/flip_frame_pump_test.cc',
        'common/spdy_to_http_converter_test.cc',
      ],
    },
  ],
}
