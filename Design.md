# mod\_spdy Design #

NOTE: this document is out of date. In the meantime, see HowItWorks for a more up-to-date description of how multiplexing support is implemented in mod\_spdy.


---


# Background #

mod\_spdy is an experimental implementation of the SPDY protocol that aims to provide SPDY support in the Apache HTTPD server.

## SPDY ##

SPDY is an experimental protocol that enables multiplexing, prioritization, and header compression of HTTP traffic. For more information on SPDY, see the [whitepaper](http://dev.chromium.org/spdy/spdy-whitepaper) and [protocol](http://dev.chromium.org/spdy/spdy-protocol) documents.

## Apache ##

Apache HTTPD is the most commonly used HTTP server on the web, used by roughly 50% of domains and 66% of the top million web sites as of January 2010, according to [Netcraft](http://news.netcraft.com/archives/2010/01/07/january_2010_web_server_survey.html).

Apache request/response processing is performed by a chain of filters. Each filter receives byte buffers from the previous filter in the filter chain, performs transformations on those buffers, and emits the transformed byte buffers to the next filter in the chain. The apache filter processing flow is described [here](http://httpd.apache.org/docs/2.2/filter.html) and additional implementation details are covered [here](http://httpd.apache.org/docs/2.2/developer/filters.html).

# Overview #

mod\_spdy is an Apache 2.x filter module that provides SPDY support in the Apache HTTPD web server. Because mod\_spdy is an Apache module, it can be loaded into currently deployed Apache HTTPD 2.x web servers using [mod\_so](http://httpd.apache.org/docs/2.2/mod/mod_so.html).

mod\_spdy provides two Apache filters: a connection-level filter on the input side, and a transcode-level filter on the output side. The input filter receives SPDY frames and emits HTTP requests, while the output filter receives HTTP responses and emits SPDY frames. Additional details about the input and output filters are provided in the Details section.

## Multiplexing ##

Due to the multiplexed nature of the SPDY protocol, an efficient SPDY implementation should support concurrent processing of requests within a single SPDY session (that is, on a single TCP connection). Current releases of Apache do not support concurrent processing of more than one request on a connection, due to the mostly serialized nature of request/response flow in the existing HTTP protocol. Thus, the initial implementation of mod\_spdy processes each SPDY stream (request) in a session (connection) serially.

Ideas to enable multiplexing in Apache are covered in MultiplexIdeas.

# Details #

## Input Data Flow ##

mod\_spdy uses Chromium's [SpdyFramer](http://code.google.com/p/mod-spdy/source/browse/trunk/src/net/spdy/spdy_framer.h) class to encode and decode SPDY frames.

On the input side, mod\_spdy uses [SpdyFramePump](http://code.google.com/p/mod-spdy/source/browse/trunk/src/mod_spdy/common/spdy_frame_pump.h) to push data into a `SpdyFramer` one frame at a time. The `SpdyFramer` publishes decoded frames to a [SpdyStreamDistributor](http://code.google.com/p/mod-spdy/source/browse/trunk/src/mod_spdy/common/spdy_stream_distributor.h), which shards SPDY frames based on their stream id. Each stream is then converted to HTTP using a [SpdyToHttpConverter](http://code.google.com/p/mod-spdy/source/browse/trunk/src/mod_spdy/common/spdy_to_http_converter.h). Finally, the HTTP data is published to an [HttpStreamVisitorInterface](http://code.google.com/p/mod-spdy/source/browse/trunk/src/mod_spdy/common/http_stream_visitor_interface.h).

**Tracking SPDY metadata**

SPDY stream metadata, such as the stream id, is lost during the transformation from SPDY to HTTP. The stream id is needed by the output filter in order to package the HTTP responses into SPDY frames, so this data must be communicated to the output filter.

In order to communicate the stream id from input filter to output filter, an `x-spdy-request-id` header is injected into the HTTP request during the transformation from SPDY to HTTP.

## Output Data Flow ##

TODO(mdsteele): Write section on output data flow:
  1. `HeaderPopulatorInterface`
  1. `OutputFilterContext`
  1. `OutputStreamInterface`

## Apache Filter Registration ##

TODO: describe `mod_spdy.cc` execution flow.

## Apache Input Filter ##

Because SPDY session state is persistent for the lifetime of the SPDY session/TCP connection, the input filter is implemented as an Apache connection-level filter. Alternatively, the input filter could be implemented as a higher level filter (e.g. a transcode filter) with the SPDY session state stored in an Apache connection configuration vector (TODO: explore the tradeoffs of these two approaches with Apache developers).

When tracking SPDY metadata (described above), we would like to annotate the Apache `request_rec` structure with the stream id so it was available to the output filter. This is less hacking than injecting a new header into the HTTP request stream. However, because the mod\_spdy input filter is a connection-level filter, the `request_rec` struct does not yet exist at the time the filter is invoked. This is one reason to consider converting the input filter to a transcode filter.

## Apache Output Filter ##

TODO(mdsteele): write this section

We should describe our interactions with sent\_bodyct so Apache devs can read this and get a good understanding on how this works.

For each HTTP response, the mod\_spdy output filter receives a structured representation of the HTTP response headers (`request_rec.headers_out`) (TODO: the Apache code appears to merge headers\_out and err\_headers\_out - do we need to do the same?) and uses those headers to populate a `spdy::SpdyHeaderBlock`, which is in turn used to construct a SYN\_REPLY frame.