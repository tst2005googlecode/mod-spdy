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
      'target_name': 'mod_static',
      'type': 'loadable_module',
      'dependencies': [
        'http_response_proto',
        '<(DEPTH)/base/base.gyp:base',
        '<(DEPTH)/third_party/apache_httpd/apache_httpd.gyp:apache_httpd',
      ],
      'include_dirs': [
        '<(DEPTH)',
      ],
      'sources': [
        'mod_static.cc',
      ],
    },
    {
      'target_name': 'http_response_proto',
      'type': '<(library)',
      'hard_dependency': 1,
      'dependencies': [
          '<(DEPTH)/third_party/protobuf2/protobuf.gyp:protobuf',
          '<(DEPTH)/third_party/protobuf2/protobuf.gyp:protoc',
      ],
      'actions': [
        {
          'action_name': 'generate_http_response_proto',
          'inputs': [
            '<(PRODUCT_DIR)/<(EXECUTABLE_PREFIX)protoc<(EXECUTABLE_SUFFIX)',
            '<(DEPTH)/mod_static/http_response.proto',
          ],
          'outputs': [
            '<(SHARED_INTERMEDIATE_DIR)/mod_static/http_response.pb.cc',
            '<(SHARED_INTERMEDIATE_DIR)/mod_static/http_response.pb.h',
          ],
          'action': [
            '<(PRODUCT_DIR)/<(EXECUTABLE_PREFIX)protoc<(EXECUTABLE_SUFFIX)',
            '<(DEPTH)/mod_static/http_response.proto',
            '--proto_path=<(DEPTH)',
            '--cpp_out=<(SHARED_INTERMEDIATE_DIR)',
          ],
        },
      ],
      'sources': [
        '<(SHARED_INTERMEDIATE_DIR)/mod_static/http_response.pb.cc',
      ],
      'include_dirs': [
        '<(SHARED_INTERMEDIATE_DIR)',
      ],
      'direct_dependent_settings': {
        'include_dirs': [
        '<(SHARED_INTERMEDIATE_DIR)',
        ],
      },
      'export_dependent_settings': [
        '<(DEPTH)/third_party/protobuf2/protobuf.gyp:protobuf',
      ]
    },
  ],
}
