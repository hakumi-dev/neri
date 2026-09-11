# HTTP serving

The `http` standard library serves text and binary responses on a loopback TCP port.
The installed launcher loads it when a source file declares `use http`.

```ruby
use http
use console
use host

def main(): Void
  let error = http.serve("127.0.0.1:8080") do |request|
    return http.text(200, "Hello, world!")
  end
  console.println(error)
  host.exit(1)
end
```

Run [the example](../examples/http.hk) with `neri examples/http.hk`, or build it
with `neri build examples/http.hk --output server` and run `./server`.
Request `http://127.0.0.1:8080/` from another terminal or browser.

The example prints `Listening on http://127.0.0.1:8080` after the socket is
bound and listening. `PORT` selects another port; `HTTP_LOG=1` enables connection
and response logs. Access logging is off by default. For example:

```sh
PORT=3000 HTTP_LOG=1 neri examples/http.hk
curl http://127.0.0.1:3000/
```

These environment variables belong to the example, not the HTTP library.
An invalid or empty `PORT` reports an error and exits with status 1.

## API

`http.serve(address: String, handler: fn(Request): Response, options: Options? = null): String` binds
`127.0.0.1:<port>`, where the port is between 1 and 65535. It serves synchronously
until programmatic stop, process termination or a listener failure. Startup and listener failures
return an error string, including the failing operation. An occupied port is a
startup failure. Individual connection failures close that connection and allow
the next request. `serve` formats a listener failure as text and returns
`"Listener stopped"` after programmatic shutdown.

`http.serveResult(address, handler, options): Failure?` exposes the same server
with null on normal shutdown and a structured failure otherwise. Invalid
configuration uses `invalid_address` or `invalid_port`. Socket failures use
`socket_error`, with an operation distinguishing `listen.open`,
`listen.configure`, `listen.bind`, `listen.start`, `listen.wait` and
`listen.accept`.

### Stop and drain

Set `options.stop` to an `http.Stop` and call its `request()` synchronously from
`onListening`, the handler or the log callback to finish serving. A request in
progress completes its response under the existing I/O deadline; then the server
closes the connection and listener and returns normally. A request from
`onListening` closes the idle listener before accepting a connection. A stop
requested before serving skips socket acquisition after configuration validation.
Stop objects remain requested and can be inspected with `isRequested()`.

This stop object is confined to the serving thread. It does not preempt an
application handler. Handler execution remains synchronous; a fatal deadline
requires the explicit option below.

`options.handleInterrupts = true` requests a process interrupt lease for the
duration of `serveResult`/`serve`. POSIX handles SIGINT and SIGTERM; Windows
handles console Ctrl+C and Ctrl+Break. The native handler only marks a lock-free
flag. Neri checks it between connections and waits at most 50 milliseconds per
idle poll, preventing a signal between the check and poll from leaving the
listener blocked indefinitely. Active request I/O retains its existing read and
write deadlines before shutdown. This does not bound application handler time.

Only one interrupt lease or interactive terminal lease may be active. POSIX
acquisition also rejects existing nondefault signal actions. Failure to acquire
returns `interrupt_unavailable` with operation `listen.interrupt`. On supported
returns the lease restores its actions; it does not overwrite a POSIX action
replaced externally while serving. Windows removes its own console handler.
Lease operations belong to the serving thread; concurrent external changes to
process signal handling are outside this contract. See
[sigaction](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/sigaction.2.html)
and [SetConsoleCtrlHandler](https://learn.microsoft.com/en-us/windows/console/setconsolectrlhandler)
for platform behavior. The signal path is verified on macOS; Windows console
delivery requires separate execution evidence.

`Options.forceExitAfterStopMilliseconds` opts into a fatal deadline of 1 through
60000 milliseconds. After `Stop.request()` or a pending owned interrupt, the
active request may finish normally until the deadline. If it is still running,
a native watchdog requests whole-process termination with `_Exit(124)`,
independently of handler cooperation.

Forced exit skips handler unwinding, user cleanup, flush callbacks and runtime
shutdown. The operating system reclaims process descriptors; close work and
scheduling mean the deadline is not a real-time termination guarantee. Child
processes remain the responsibility of their process-group supervisor. See
[`_exit(2)`](https://man7.org/linux/man-pages/man2/exit.2.html) for Linux's
process-wide exit semantics.

Without this option, stop is cooperative and cannot bound a handler that loops
or blocks forever.

### Callbacks

`http.Options` provides optional callbacks for listener startup and logging:

- `onListening: (fn(String): Void)?` receives the address once, after successful
  bind and listen and before accepting connections. Startup failures skip it.
- `log: (fn(String): Void)?` receives a response status after a successful write,
  or a connection setup, incomplete-read, or write-failure message. Response logs
  use the actual wire status, including rejected requests and substituted 500s.
  They contain no request paths, queries, headers, or bodies. A successful write
  means the local socket accepted the bytes, not that the client consumed them.

Both callbacks run synchronously. The library prints nothing itself; applications
choose a destination and format. Startup and listener errors are returned to the
caller, which decides how to report them.

```ruby
let options = new http.Options()
options.onListening = fn(address)
  console.println("Listening on http://" + address)
end
options.log = fn(message)
  console.println(message)
end
```

### Requests and responses

`Request` exposes `method`, `path`, and `query` as strings. Accepted requests have
method `GET` or `HEAD`, preserving the received method for the handler. The path keeps percent escapes exactly as received, and ends before
the first `?`. The query excludes that separator and remains encoded. Neither
field is decoded or normalized. Mutating the request changes only the handler's
local request object.

`Request.headers` and `Response.headers` are `http.Headers` collections.
`add(name, value): Bool` validates an ASCII token name and an ASCII value
(horizontal tabs are allowed), returning false without mutation on invalid
input or a capacity limit. Each collection holds at most 64 field lines and
7168 serialized bytes, counting `name + ": " + value + CRLF`.
`values(name): String[]` returns matching values in insertion order using
case-insensitive names. Lines remain separate, including repeated request
fields; callers apply the relevant field's combination rules. `count()`,
`nameAt(index)` and `valueAt(index)` expose ordered iteration.

Responses accept one line per field name, with repeated `Set-Cookie` lines
permitted. Combine list-valued response fields explicitly in one value.
`Content-Type` overrides the default; `Allow` can describe the application's
supported methods. Content-Length, Transfer-Encoding, Connection, Trailer,
Upgrade, Keep-Alive and Proxy-Connection belong to the transport. Supplying one
of these fields, or repeating another response field, substitutes a plain 500
response and discards application headers. These rules also apply to HEAD.

```ruby
let response = http.text(200, "<h1>Hello</h1>")
response.headers.add("Content-Type", "text/html; charset=utf-8")
response.headers.add("ETag", "\"page-v1\"")
```

`http.text(status: Int, body: String): Response` constructs a response with an
explicit status and UTF-8 body. Text responses default to `text/plain; charset=utf-8`, a byte
`Content-Length`, and `Connection: close`. Status 204 and 304 omit the body and
content length; status 205 sends an empty body. Statuses outside 200–599 or bodies
larger than 1 MiB produce status 500 instead of the supplied response.

`http.binary(status: Int, body: Byte[]): Response` retains the managed byte array
and defaults to `application/octet-stream`. Set `response.headers` to declare a
specific media type. Bytes are sent unchanged, including NUL and invalid UTF-8.
`Response.bodyBytes`, when present, takes precedence over `body`. The array stays
alive through synchronous sending; applications finish mutations before handing
the response to the transport. The same 1 MiB limit and bodyless-status rules
apply to binary responses.

`http.wire(response, head): EncodedResponse` prepares the exact header string and
either a text body or retained byte array for inspection without opening a
socket. Its `headers`, `text` and optional `bytes` fields describe the validated
response, including any substituted 500. HEAD and bodyless statuses contain no
payload. Sending uses a fixed 1024-byte scratch buffer and one absolute deadline
for both headers and body. `writeText(fd, text)` and `writeBytes(fd, bytes)` are
low-level Boolean writers with a separate two-second deadline per invocation;
callers own the socket.

## Structured transport failures

`http.Failure` carries `code`, `operation` and `detail`. Codes identify causes
without parsing human-readable text. Detail contains the available platform
message for socket/clock failures and is otherwise empty; it belongs in internal
diagnostics, not public response bodies.

`parse(head, allowHead = false)` exposes `Parsed.failure`, which is null on success.
The default preserves the GET-only method contract for existing low-level
adapters. Passing `true` accepts GET and HEAD; the standard server selects this
mode and preserves HEAD for its handler. Rejections
distinguish invalid request lines, targets, fields, Host and Content-Length from
unsupported versions, methods, expectations and body framing. General malformed
input uses `malformed_request`. These are string codes in a typed result object;
they do not provide exhaustive language-level matching.

`readHead(fd, timeoutMilliseconds = 2000)` preserves `text` and HTTP `status`,
and exposes `failure` and `bytesRead`. Read causes are `timeout`, `peer_closed`,
`socket_error`, `clock_error`, `invalid_header_bytes`, `header_limit` and
`invalid_timeout`. `bytesRead` counts bytes consumed from the socket, including
any bytes received beyond the terminating header line in the final read.

`writeTextResult(fd, text, timeoutMilliseconds = 2000)` and
`writeBytesResult(fd, bytes, timeoutMilliseconds = 2000)` return `WriteResult`
with `bytesWritten` and optional `failure`. Null failure means all bytes were
accepted by the local socket. A failure preserves the partial count and reports
`timeout`, `socket_error`, `clock_error`, `write_stalled` (zero-byte progress), or
`invalid_timeout`. A write error alone does not establish orderly peer closure.
The Boolean writer helpers project this result's success.

Timeouts range from zero to 2147483647 milliseconds. Zero gives an already
expired deadline for nonempty I/O; an empty write succeeds without socket I/O.
Operations identify `read`, `write`, `read.wait`, `write.wait`, `clock` or `parse`.
Interrupted and would-block system calls retry within the same absolute deadline.
These helpers retain caller-owned sockets; they never close them.

HEAD invokes the same handler with method `HEAD`. The handler supplies the
representation as for GET; the transport sends its status and headers, including
the representation's byte length, and omits all body bytes. This also applies to
error responses. Applications control resource lookup and the resulting status.

## Protocol and limits

The syntax and framing rules follow [RFC 9112 sections 2, 5 and 6](https://www.rfc-editor.org/rfc/rfc9112.html),
with HEAD and bodyless response semantics from [RFC 9110](https://www.rfc-editor.org/rfc/rfc9110.html#section-9.3.2).
The limits below define the supported subset. Invalid version syntax receives
400; a syntactically valid unsupported version receives 505. Whitespace before
a field name or its colon is rejected, and field lines use CRLF.

The server implements a restricted HTTP/1.1 origin-form request protocol:

- One bodyless GET or HEAD request per connection, processed sequentially.
- CRLF line endings and ASCII request lines and header values.
- Exactly one nonempty Host header. Header names are case-insensitive.
- Content-Length may be absent or a single decimal zero value.
- Request targets start with `/` and contain valid URI characters and percent escapes.
- The request line, headers, and final blank line occupy at most 8192 bytes.
- Request fields also obey the `Headers` count and serialized-byte limits;
  exceeding those limits rejects the request with 400.
- One absolute two-second header-read deadline, including fragmented input.
- One absolute two-second response-write deadline, including partial writes.

Malformed requests and unsupported body framing receive 400. Unsupported methods
receive 405 with `Allow: GET, HEAD`; unsupported versions receive 505; Expect receives
417; oversized headers receive 431; incomplete headers that exceed the deadline
receive 408 when the connection remains writable. Responses to HEAD omit a body.
EOF and socket failures close the connection. Extra requests on the same connection
are not dispatched. Request bodies, keep-alive, routing, TLS, HTTP/2, streaming,
and concurrent handlers are outside this API.

The handler runs synchronously. The [stop and drain policy](#stop-and-drain) permits
an explicit whole-process deadline after a stop request. Fatal panic or
forced process termination does not unwind application scopes; the operating
system reclaims the process's sockets. On supported returns and I/O error paths,
the Neri library closes each accepted descriptor explicitly.

`Options.onConnection` receives `result.Result<Exchange, ExchangeFailure>` once
after the accepted socket is closed. `Exchange` records consumed request bytes,
written response bytes, and response status. `ExchangeFailure` preserves the
primary failure, any failure sending a rejection response, and an ordered
`closeFailures` collection. Binary responses and response headers share one write deadline.
`serveOutcome` exposes listener completion through `result.Result`.

## Implementation boundary

Protocol parsing, response construction, retries, deadlines, and descriptor
ownership are implemented in [Neri](../stdlib/http.hk). A small
[C adapter](../native/platform/socket_posix.c) supplies platform socket layouts,
constants, monotonic time, and individual system calls. These unsafe imports
require runtime ABI 1.20 with sockets, interrupts, socket-close-result, and drain
features. The toolchain includes and
checksums the standard-library source alongside its native artifacts.

## Loopback client

The `httpclient` module provides a bounded plain HTTP/1.1 client for local
integration tests. `get`, `head`, and `request` connect only to `127.0.0.1` at
the supplied port. The request target is an origin-form string beginning with
`/` with an optional query and no fragment; the client sends its bytes unchanged, including encoded path separators,
dot segments, and the query. This preserves the target syntax whose semantics
belong to the server under
[RFC 9112 section 3.2.1](https://www.rfc-editor.org/rfc/rfc9112.html#section-3.2.1).

Each exchange uses one connection and sends `Connection: close`. Responses
retain their body as bytes and retain repeated field lines in order. The first
matching field is available through `Response.header`; all matching fields are
available through `Response.headerValues`. `Options` bounds the absolute I/O
deadline, response header, and response body. The client accepts
`Content-Length` framing and close-delimited responses, rejects transfer coding
and repeated length fields, and applies the response-body rules from
[RFC 9112 section 6.3](https://www.rfc-editor.org/rfc/rfc9112.html#section-6.3).

Defaults are 4 seconds, 16 KiB of response headers and 8 MiB of body. Response
headers contain at most 128 field lines. Configurable limits are at most 1 MiB
for response headers and 128 MiB for the body; the complete encoded request is
bounded to 1 MiB. Status and length fields use strict decimal syntax. Interim
1xx responses report `unsupported_interim_response`; extra bytes beyond a
declared body report `extra_body`. Both connection and response deadlines
report `timeout`. Cleanup preserves a socket-close failure alongside an
existing exchange failure.

The client requires runtime ABI 1.22 and `SOCKET_ENDPOINTS`. The runtime also
exposes bound-port discovery for test listeners that bind port zero, so a
fixture can retain its listener while reporting the assigned port.

The module intentionally covers loopback plain HTTP. It does not resolve host
names, negotiate TLS, follow redirects, pool connections, or decode transfer
and content codings.
