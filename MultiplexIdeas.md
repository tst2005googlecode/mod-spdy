# Multiplexing SPDY in Apache #

**NOTE: this document is out of date. mod\_spdy currently supports multiplexing using a variation of the proposal described in this document. In the meantime, see HowItWorks for a more up-to-date description of how multiplexing support is implemented in mod\_spdy.**


---


# Background #

For background on `mod_spdy`, see [mod\_spdy design](Design.md).

Due to the multiplexed nature of [the SPDY protocol](http://dev.chromium.org/spdy/spdy-protocol), an efficient SPDY implementation should support concurrent processing of requests within a single SPDY session (that is, on a single TCP connection). Current releases of Apache do not support concurrent processing of more than one request on a connection, due to the mostly serialized nature of request/response flow in the existing HTTP protocol.

Adding support for SPDY multiplexing in Apache is a tricky issue. Apache connection processing is fundamentally single-threaded. A comment in `ap_mpm.h` explains the contract between an MPM and its connections:

> At any instant (the MPM) guarantees a 1:1 mapping of threads (to) ap\_process\_connection invocations.

> Note: In the future it will be possible for ap\_process\_connection to return to the MPM prior to finishing the entire connection; and the MPM will proceed with asynchronous handling for the connection; in the future the MPM may call ap\_process\_connection again -- but does not guarantee it will occur on the same thread as the first call.

In order to multiplex SPDY in Apache, we would either need to break this contract, or create a separate "connection" for each stream in a session. Both of these approaches are explored in the ideas section, below.

# Current Proposal #

The current idea on the table (which has yet to be prototyped) is for us to create our own thread pool in each child process, and have each "master" connection (which represents a SPDY connection) farm out "slave" connections (which represent SPDY streams) to the thread pool, which would then invoke ap\_process\_connection, allowing Apache to handle the fake, slave connection as though it were a normal HTTP connection, and multiplex the results back along the master SPDY connection.

Below, we provide more detail into how this would be implemented.

## Relevant Apache Hooks ##

These are the Apache hooks that we would need to use, listed in the order that they are invoked by Apache.

### `child_init` ###

> void, `APR_HOOK_MIDDLE`

> Create our thread pool for this process, checking the server config to
> determine how large it should be.  Store it somewhere accessible to our
> master connection processing function (probably a global variable -- I think
> that's safe since it's per-process).

### `pre_connection` ###

> run-all, `APR_HOOK_LAST`

> Check `ap_get_module_config(c->conn_config, &spdy_module)` to see if we already
> have something there.  If so, this must be a slave SPDY connection, so
> install our connection-layer filters and return `DONE` to prevent
> `core_pre_connection` from inserting the core network filters (the slave
> connection will have been set up such that mod\_ssl will ignore it, so that's
> not a concern).  Otherwise, do nothing and return `DECLINED`.

### `process_connection` ###

> run-first, `APR_HOOK_FIRST`

> Check `ap_get_module_config(c->conn_config, &spdy_module)` to see if this is a
> slave SPDY connection; if so, return `DECLINED`.  Otherwise, determine (via
> hocus-pocus) whether this is an HTTP connection rather than a (master) SPDY
> connection; if so, return `DECLINED`.  Otherwise, run our master connection
> processing function, and return `OK` when it's done.

### `insert_filter` ###

> void, `APR_HOOK_MIDDLE`

> Check `ap_get_module_config(c->conn_config, &spdy_module)` to see if this is a
> slave SPDY connection; if so, insert our protocol-layer output filters.
> Otherwise, do nothing.

## Filters ##

These are the filters we would need to have.  They are inserted only for slave SPDY connections; we don't add any filters for master SPDY connections or for HTTP connections.

### `spdy_to_http` ###

> input, `AP_FTYPE_NETWORK`

> Pulls SPDY frames from the input queue, and passes equivalent HTTP down the
> chain.  Its next filter should always be `NULL`.

### `anti_chunking` ###

> output, `AP_FTYPE_PROTOCOL - 1`

> Slips in just before the core `HTTP_HEADER` filter and ensures that chunking is
> disabled.  A gross hack, but it should work.

### `http_to_spdy` ###

> output, `AP_FTYPE_TRANSCODE`

> Converts `r->headers_out` and body data into SPDY frames.

### `spdy_final_output` ###

> output, `AP_FTYPE_NETWORK`

> Puts bytes onto the output queue.  Its next fitler should always be `NULL`.

## Open Questions ##

  * Should the input/output queues between the master and slave connections store SPDY frame objects, or raw bytes?  On the input side, I feel like frame objects make the most sense.  On the output side, we can really only do frame objects if the `http_to_spdy` TRANSCODE filter is the final output filter, which means we can't have any connection-layer output filters.  OTOH, that's probably okay -- the only connection-layer filters in the main codebase appear to be core and ssl, neither of which apply here.

  * How do we make sure that the input headers on the variable streams are all compressed in the same context and in the proper order?  Possibly the master connection should be the one to decompress the headers, and then attach the resulting header map to the slave connection context.

  * How do we make sure that the output headers on the various streams are all compressed in the same context and in the proper order?  Possibly the slave connection should place uncompressed SPDY frames on its output queue, and the master should then compress them as it sends them to the client.  That would probably be an argument in favor of having SPDY frame objects on the output queue rather than raw bytes.

  * What if we need to cancel a stream when it's half done?  Can we tell the thread to abort?  Probably we can set some flag that will cause our slave filters to abort the connection next time they're invoked.

  * What do we need to do in order to use `apr_thread_pool_create`?

  * When creating the slave conn\_rec object, we need to supply a unique connection ID.  The MPM is normally responsible for this, and every MPM does it differently.  Is there a non-hacky way to make sure our fake IDs won't conflict with those of real connections?  If not, maybe a semi-non-hacky way?  Most MPMs seem to use positive numbers; maybe ours could be negative?  We must also make sure that our fake IDs don't conflict with fake IDs we make on other child processes.

  * When creating the slave conn\_rec object, we need to supply things like a scoreboard (`c->sbh`) and a socket (for `ap_process_connection`).  Can we get away with passing `NULL` for those, and if not, what do we need to supply to make this work?

  * How do we deal with priorities?  For starters, we can make the task queue for assigning new streams to threads in our thread pool be a priority queue keyed on stream priority (`apr_thread_pool_t` already supports this behavior).  Ideally, we would also give important stream threads more processor time, but I don't think APR gives us direct control over this.  Possibly we could give them priority when pulling frames off of the output queues to send to the client; if the final slave output filter blocks on output until its output queue is empty, this could have the effect of giving more processor time to important streams.

  * How do we know if an incoming (non-slave) connection is HTTP or SPDY?  Of course, this is pretty much up to the SPDY team, but we should consider whether we are really going to be able to determine this from within the `process_connection` hook.

# Older Ideas #

## Loopback connection for each SPDY stream ##

Apache supports processing of a single request per connection. Thus, a simple (though hacky) way to enable multiplexing in Apache is to create a new TCP connection back to the Apache server for each new SPDY stream.

This is undesirable in that the Apache server is communicating with itself through the OS over TCP. There might be cases where this could fail, such as if a firewall configuration prevents the Apache server from connecting to itself (or from making outbound connections). In addition, this technique has some other undesirable side-effects, such as the fact that these new incoming connections will have a request IP of the local server instead of the client.

One way to support the loopback approach is to use the `create_connection` (or `process_connection`?) hook to intercept new connections. If the new connection is a SPDY connection with an origin IP different from the Apache server, a SPDY dispatcher would accept the connection. The SPDY dispatcher would parse incoming SPDY frames. For each new SPDY stream, the dispatcher would open a TCP connection to the Apache server and push all SPDY frames associated with that stream onto the new connection. Likewise, the dispatcher would pull SPDY frames from that connection and multiplex them onto its own connection back to the client.

Though this could be made to work, it is preferable to keep the new connection creation entirely inside of the Apache process. We explore ideas that stay entirely in-process below.

## Faking conn\_rec structs ##

Alternatively, we could have a pool of threads, where each active SPDY stream is processed in its own thread.

In this approach, we would use something akin to the SPDY distributor described above. Instead of routing each SPDY stream over its own TCP connection, we would instead create a new `conn_rec` instance, using `ap_run_create_connection` and `ap_run_pre_connection`. We would make some modifications to the resulting `conn_rec` struct, namely replacing the input socket bucket with a bucket that read from a thread-safe queue, and replacing the output filter which wrote to a different thread-safe queue instead of the socket. We would then invoke `ap_run_process_connection` on the `conn_rec` in a new thread, and push frames for the SPDY stream into the thread-safe queue.

This is more desirable than the new TCP connection approach described above in that it stays in-process, but also considerably more fragile, since it makes assumptions about what the core does in the `ap_run_create_connection` and `ap_run_pre_connection` hooks.

(Question: APR has functions for creating and manipulating thread pools in [apr\_thread\_pool.h](http://apr.apache.org/docs/apr/trunk/apr__thread__pool_8h.html); could a filter/handler/hook create a thread pool and put tasks on it, or would that break everything (depending on the MPM)?)

## New MPM ##

A more robust approach might be to create a new MPM based on the existing async MPMs that uses an approach similar to the thread pool above, but at the MPM level. The downside to this approach is that the resulting module would only be pluggable in Apache 2.3.x. Prior to 2.3.x, MPMs are not pluggable.

# Other Notes #

**Async Handling in Apache**

In the Apache 2.2 branch, the [event](http://httpd.apache.org/docs/2.2/mod/event.html) MPM does support basic asynchronous handling (in the 2.3 branch, the `simple` MPM supports asynchronous handling as well).

Other developers have extended the `event` MPM to support a more asynchronous processing model, as described in [this thread](http://mail-archives.apache.org/mod_mbox/httpd-modules-dev/200903.mbox/<49B4E5A8.4090209@gmail.com>).

In order to take advantage of async handling in these MPMs, however, the generator needs to support async handling, and the filters need to be compatible with async handling. Most filters do appear to be async-compatible, with the exception of `mod_ssl` which is a "clogging" filter.

Most generators do not support async handling, however. We would like to support SPDY multiplexing for all generators so users can just drop in `mod_spdy` and leverage SPDY in their Apache instance, regardless of their configuration. Thus, each SPDY stream needs to be processed in its own thread. Since there are potentially multiple concurrent SPDY streams for a single connection, this does appear to break the threading contract in `ap_mpm.h`. If we treat each SPDY stream within a SPDY session as its own connection, however, it might be possible to enable multiplexing in Apache without breaking this contract.