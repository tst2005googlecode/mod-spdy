# Copyright 2014 Google Inc.
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
  'variables': {
    'apache_root': '<(DEPTH)/third_party/apache/util_cookies',
    'apache_src_root': '<(apache_root)/src',
  },
  'targets': [
    {
      'target_name': 'include',
      'type': 'none',
      'direct_dependent_settings': {
        'include_dirs': [
          '<(apache_root)/include',
        ],
      },
      'dependencies': [
        '<(DEPTH)/third_party/apache/apr/apr.gyp:include',
        '<(DEPTH)/third_party/apache/httpd/httpd.gyp:include',
      ],
      'export_dependent_settings': [
        '<(DEPTH)/third_party/apache/apr/apr.gyp:include',
        '<(DEPTH)/third_party/apache/httpd/httpd.gyp:include',
      ],
    },
    {
      'target_name': 'util_cookies',
      'type': '<(library)',
      'dependencies': [
        'include',
      ],
      'export_dependent_settings': [
        'include',
      ],
      'sources': [
        'src/util_cookies.c',
      ],
    }
  ],
}
