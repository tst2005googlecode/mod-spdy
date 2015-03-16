All of `mod_spdy`'s configuration options begin with the prefix "`Spdy`", and most of them can be used either at the top level or within a `<VirtualHost>` context.

# Turning the module on and off #

### Enabling the module ###

Once `mod_spdy` is installed, you must enable it by adding the following
directive to your Apache configuration:
```
SpdyEnabled on
```
This will cause your server to advertise SPDY support to clients, and to use
SPDY when connecting with SPDY-enabled browsers (such as Chrome, or recent
versions of Firefox) over SSL connections.

Note that `mod_spdy` is still a "beta", and for the time being it is _off_ by
default when installed, unless you explicitly enable it with a `SpdyEnabled`
directive.  Once the module is more stable, we may change it to being on by
default once installed.

### Disabling the module ###

To turn `mod_spdy` off completely, use:
```
SpdyEnabled off
```
When disabled, `mod_spdy` will do nothing at all; in particular, it will not
spawn its thread pool, serve requests via SPDY, or advertise SPDY support to
clients.

# Tuning the module #

### Changing the size of the thread pool ###

To tune the size of the thread pool `mod_spdy` uses for processing concurrent
SPDY streams, use the directive:
```
SpdyMaxThreadsPerProcess n
```
where `n` is the number of threads to use on each child process.  Note that if
you are using a threaded MPM, this thread pool will be _shared_ by all
connections on each process.  At the moment, the default value is 5 (rather
conservative), but this will likely change as we do more performance testing.  This directive is valid only at the top level (not within a `<VirtualHost>` directive).

### Changing the limit on simultaneous streams ###

The SPDY protocol allows endpoints to limit the number of simultanoues SPDY
streams they are willing to have open at a time on one connection, for example
to limit memory consumption.  To set the limit used by `mod_spdy`, use the
directive:
```
SpdyMaxStreamsPerConnection n
```
where `n` is the maximum number of SPDY streams (i.e. requests) that can be
active at any given time on a single connection.  It is recommended to keep
this limit reasonably high; the current default value is 100.

### Changing Sever Push Limits ###

The SPDY protocol supports server-initiated content push. By default in `mod_spdy`, only
the initial client-initiated request can push content; pushed content can
not result in further pushed content, because this could potentially result in unwanted
extra content in the best case and infinite recursion in the worst. However, you can adjust
this restriction with the directive:
```
SpdyMaxServerPushDepth n
```
where `n` is the maximum distance from the initial client request that a push
can be initiated. A push depth of 1, the default, allows only the initially
requested content to push more content. A push depth of 2 will allow that
pushed content to result in more pushes. Setting this value to 0 effectively
turns off server push. Note that recursive server push is currently allowed
and should be watched for if you set this value to a high number: a.html
could push b.html which can in turn push a.html again which would push b.html,
etc.

# Debugging #

`mod_spdy` has several config options useful for debugging the module, which
begin with the prefix "`SpdyDebug`".  It is
_not_ recommended to use these options in production.

### Enabling more verbose logging ###

To enable more verbose logging output from `mod_spdy`, beyond what is normally
logged at `LogLevel debug`, use:
```
SpdyDebugLoggingVerbosity n
```
where `n` is a non-negative integer.  Zero is the default, and does not enable
any new logging; 5 will enable many additional log messages.  Note that this
directive will have no effect unless Apache's `LogLevel` is set to `debug`.
This directive is valid only at the top level (not within a `<VirtualHost>` directive).

### Debugging SPDY without SSL ###

Normally SPDY is only ever used for SSL connections (`https` URLs).  To tell
`mod_spdy` to use SPDY even for non-SSL connections (`http` URLs), use:
```
SpdyDebugUseSpdyForNonSslConnections n
```
where `n` is the SPDY version to use (either `2` or `3`).
**Note that this will break `http` URLs for browsers,** so _never_ use it in
production.  It can be useful, for example, if you want to debug the SPDY
connection with a tool such as Wireshark without having to deal with
encryption.  Running Chrome with the `--use-spdy=no-ssl` flag will allow Chrome
to talk to `mod_spdy` in this mode.  This option is off by default; you can explicitly disable it with `SpdyDebugUseSpdyForNonSslConnections off`.