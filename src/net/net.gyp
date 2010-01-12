# Copyright (c) 2009 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'variables': {
    'chromium_code': 1,
  },
  'targets': [
    {
      'target_name': 'net_base',
      'type': '<(library)',
      'dependencies': [
        '../base/base.gyp:base',
        '../base/base.gyp:base_i18n',
        '../build/temp_gyp/googleurl.gyp:googleurl',
        '../third_party/icu/icu.gyp:icui18n',
        '../third_party/icu/icu.gyp:icuuc',
        '../third_party/zlib/zlib.gyp:zlib',
        'net_resources',
      ],
      'sources': [
        'base/address_family.h',
        'base/address_list.cc',
        'base/address_list.h',
        'base/auth.h',
        'base/cache_type.h',
        'base/cert_database.h',
        'base/cert_database_mac.cc',
        'base/cert_database_nss.cc',
        'base/cert_database_win.cc',
        'base/cert_status_flags.cc',
        'base/cert_status_flags.h',
        'base/cert_verifier.cc',
        'base/cert_verifier.h',
        'base/cert_verify_result.h',
        'base/completion_callback.h',
        'base/connection_type_histograms.cc',
        'base/connection_type_histograms.h',
        'base/cookie_monster.cc',
        'base/cookie_monster.h',
        'base/cookie_options.h',
        'base/cookie_policy.cc',
        'base/cookie_policy.h',
        'base/cookie_store.h',
        'base/data_url.cc',
        'base/data_url.h',
        'base/directory_lister.cc',
        'base/directory_lister.h',
        'base/dns_util.cc',
        'base/dns_util.h',
        'base/escape.cc',
        'base/escape.h',
        'base/ev_root_ca_metadata.cc',
        'base/ev_root_ca_metadata.h',
        'base/file_stream.h',
        'base/file_stream_posix.cc',
        'base/file_stream_win.cc',
        'base/filter.cc',
        'base/filter.h',
        'base/fixed_host_resolver.cc',
        'base/fixed_host_resolver.h',
        'base/gzip_filter.cc',
        'base/gzip_filter.h',
        'base/gzip_header.cc',
        'base/gzip_header.h',
        'base/host_cache.cc',
        'base/host_cache.h',
        'base/host_resolver.cc',
        'base/host_resolver.h',
        'base/host_resolver_impl.cc',
        'base/host_resolver_impl.h',
        'base/host_resolver_proc.cc',
        'base/host_resolver_proc.h',
        'base/https_prober.h',
        'base/https_prober.cc',
        'base/io_buffer.cc',
        'base/io_buffer.h',
        'base/keygen_handler.h',
        'base/keygen_handler_mac.cc',
        'base/keygen_handler_nss.cc',
        'base/keygen_handler_win.cc',
        'base/listen_socket.cc',
        'base/listen_socket.h',
        'base/load_flags.h',
        'base/load_log.h',
        'base/load_log.cc',
        'base/load_log_event_type_list.h',
        'base/load_log_util.cc',
        'base/load_log_util.h',
        'base/load_states.h',
        'base/mime_sniffer.cc',
        'base/mime_sniffer.h',
        'base/mime_util.cc',
        'base/mime_util.h',
        # TODO(eroman): move this into its own test-support target.
        'base/mock_host_resolver.cc',
        'base/mock_host_resolver.h',
        'base/net_error_list.h',
        'base/net_errors.cc',
        'base/net_errors.h',
        'base/net_module.cc',
        'base/net_module.h',
        'base/net_util.cc',
        'base/net_util.h',
        'base/net_util_posix.cc',
        'base/net_util_win.cc',
        'base/network_change_notifier.cc',
        'base/network_change_notifier.h',
        'base/network_change_notifier_helper.cc',
        'base/network_change_notifier_helper.h',
        'base/network_change_notifier_linux.cc',
        'base/network_change_notifier_linux.h',
        'base/network_change_notifier_mac.cc',
        'base/network_change_notifier_mac.h',
        'base/network_change_notifier_win.cc',
        'base/network_change_notifier_win.h',
        'base/nss_memio.c',
        'base/nss_memio.h',
        'base/platform_mime_util.h',
        # TODO(tc): gnome-vfs? xdgmime? /etc/mime.types?
        'base/platform_mime_util_linux.cc',
        'base/platform_mime_util_mac.cc',
        'base/platform_mime_util_win.cc',
        'base/registry_controlled_domain.cc',
        'base/registry_controlled_domain.h',
        'base/scoped_cert_chain_context.h',
        'base/ssl_cert_request_info.h',
        'base/ssl_client_auth_cache.cc',
        'base/ssl_client_auth_cache.h',
        'base/ssl_config_service.cc',
        'base/ssl_config_service.h',
        'base/ssl_config_service_defaults.h',
        'base/ssl_config_service_mac.cc',
        'base/ssl_config_service_mac.h',
        'base/ssl_config_service_win.cc',
        'base/ssl_config_service_win.h',
        'base/ssl_info.h',
        'base/transport_security_state.cc',
        'base/transport_security_state.h',
        'base/sys_addrinfo.h',
        'base/telnet_server.cc',
        'base/telnet_server.h',
        'base/test_completion_callback.h',
        'base/upload_data.cc',
        'base/upload_data.h',
        'base/upload_data_stream.cc',
        'base/upload_data_stream.h',
        'base/wininet_util.cc',
        'base/wininet_util.h',
        'base/winsock_init.cc',
        'base/winsock_init.h',
        'base/x509_certificate.cc',
        'base/x509_certificate.h',
        'base/x509_certificate_mac.cc',
        'base/x509_certificate_nss.cc',
        'base/x509_certificate_win.cc',
      ],
      'export_dependent_settings': [
        '../base/base.gyp:base',
      ],
      'conditions': [
        [ 'OS == "linux"', {
          'dependencies': [
            '../build/linux/system.gyp:gconf',
            '../build/linux/system.gyp:gdk',
            '../build/linux/system.gyp:nss',
          ],
        }],
        [ 'OS == "win"', {
            'sources/': [ ['exclude', '_(mac|linux|posix)\\.cc$'] ],
          },
          {  # else: OS != "win"
            'sources!': [
              'base/wininet_util.cc',
              'base/winsock_init.cc',
            ],
          },
        ],
        [ 'OS == "linux"', {
            'sources/': [ ['exclude', '_(mac|win)\\.cc$'] ],
          },
          {  # else: OS != "linux"
            'sources!': [
              'base/cert_database_nss.cc',
              'base/keygen_handler_nss.cc',
              'base/nss_memio.c',
              'base/nss_memio.h',
              'base/x509_certificate_nss.cc',
            ],
            # Get U_STATIC_IMPLEMENTATION and -I directories on Linux.
            'dependencies': [
              '../third_party/icu/icu.gyp:icui18n',
              '../third_party/icu/icu.gyp:icuuc',
            ],
          },
        ],
        [ 'OS == "mac"', {
            'sources/': [ ['exclude', '_(linux|win)\\.cc$'] ],
            'link_settings': {
              'libraries': [
                '$(SDKROOT)/System/Library/Frameworks/Security.framework',
                '$(SDKROOT)/System/Library/Frameworks/SystemConfiguration.framework',
              ]
            },
          },
        ],
      ],
    },
    {
      'target_name': 'net',
      'type': '<(library)',
      'dependencies': [
        '../base/base.gyp:base',
        '../base/base.gyp:base_i18n',
        '../build/temp_gyp/googleurl.gyp:googleurl',
        '../third_party/icu/icu.gyp:icui18n',
        '../third_party/icu/icu.gyp:icuuc',
        '../third_party/zlib/zlib.gyp:zlib',
        'net_base',
        'net_resources',
      ],
      'sources': [
        'flip/flip_bitmasks.h',
        'flip/flip_frame_builder.cc',
        'flip/flip_frame_builder.h',
        'flip/flip_framer.cc',
        'flip/flip_framer.h',
        'flip/flip_io_buffer.cc',
        'flip/flip_io_buffer.h',
        'flip/flip_network_transaction.cc',
        'flip/flip_network_transaction.h',
        'flip/flip_protocol.h',
        'flip/flip_session.cc',
        'flip/flip_session.h',
        'flip/flip_session_pool.cc',
        'flip/flip_session_pool.h',
        'flip/flip_stream.cc',
        'flip/flip_stream.h',
        'flip/flip_transaction_factory.h',
      ],
      'export_dependent_settings': [
        '../base/base.gyp:base',
      ],
      'include_dirs': [
        # flip_session.h includes some testing code...
        '<(DEPTH)/testing/gtest/include'
      ],
      'conditions': [
        [ 'OS == "linux"', {
          'dependencies': [
            '../third_party/chromium/src/build/linux/system.gyp:gconf',
            '../third_party/chromium/src/build/linux/system.gyp:gdk',
            '../third_party/chromium/src/build/linux/system.gyp:nss',
          ],
        }],
        [ 'OS == "win"', {
            'sources/': [ ['exclude', '_(mac|linux|posix)\\.cc$'] ],
          },
        ],
        [ 'OS == "linux"', {
            'sources/': [ ['exclude', '_(mac|win)\\.cc$'] ],
          },
          {  # else: OS != "linux"
            # Get U_STATIC_IMPLEMENTATION and -I directories on Linux.
            'dependencies': [
              '../third_party/icu/icu.gyp:icui18n',
              '../third_party/icu/icu.gyp:icuuc',
            ],
          },
        ],
        [ 'OS == "mac"', {
            'sources/': [ ['exclude', '_(linux|win)\\.cc$'] ],
            'link_settings': {
              'libraries': [
                '$(SDKROOT)/System/Library/Frameworks/Security.framework',
                '$(SDKROOT)/System/Library/Frameworks/SystemConfiguration.framework',
              ],
            },
          },
        ],
      ],
    },
    {
      'target_name': 'net_resources',
      'type': 'none',
      'msvs_guid': '8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942',
      'rules': [
        {
          'rule_name': 'grit',
          'extension': 'grd',
          'inputs': [
            '../tools/grit/grit.py',
          ],
          'outputs': [
            '<(SHARED_INTERMEDIATE_DIR)/net/grit/<(RULE_INPUT_ROOT).h',
            '<(SHARED_INTERMEDIATE_DIR)/net/<(RULE_INPUT_ROOT).rc',
            '<(SHARED_INTERMEDIATE_DIR)/net/<(RULE_INPUT_ROOT).pak',
          ],
          'action':
            ['python', '<@(_inputs)', '-i', '<(RULE_INPUT_PATH)', 'build', '-o', '<(SHARED_INTERMEDIATE_DIR)/net'],
          'message': 'Generating resources from <(RULE_INPUT_PATH)',
        },
      ],
      'sources': [
        'base/net_resources.grd',
      ],
      'direct_dependent_settings': {
        'include_dirs': [
          '<(SHARED_INTERMEDIATE_DIR)/net',
        ],
      },
      'conditions': [
        ['OS=="win"', {
          'dependencies': ['../build/win/system.gyp:cygwin'],
        }],
      ],
    },
    {
      'target_name': 'flip_server_lib',
      'type': '<(library)',
      'dependencies': [
        'net',
        '../base/base.gyp:base',
      ],
      'defines': [
        'CHROMIUM',
      ],
      'sources': [
        'tools/flip_server/balsa_frame.cc',
        'tools/flip_server/balsa_headers.cc',
        'tools/flip_server/http_message_constants.cc',
        'tools/flip_server/simple_buffer.cc',
      ],
    },
  ],
}

# Local Variables:
# tab-width:2
# indent-tabs-mode:nil
# End:
# vim: set expandtab tabstop=2 shiftwidth=2: