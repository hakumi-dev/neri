# HTTP serving

The `http` standard library serves plain-text responses on a loopback TCP port.
The installed launcher loads it when a source file declares `use http`.

```ruby
use http
use console
use host

def main(): Void
  let error = http.serve("127.0.0.1:8080", fn(request)
    return http.text(200, "Hello, world!")
  end)
  console.println(error)
  host.exit(1)
end
```

Run [the example](../examples/http.hk) with `neri examples/http.hk`, or build it
with `neri build examples/http.hk --output server` and run `./server`.
Request `http://127.0.0.1:8080/` from another terminal or browser.

## API

`http.serve(address: String, handler: fn(Request): Response): String` binds
`127.0.0.1:<port>`, where the port is between 1 and 65535. It serves synchronously
until process termination or a listener failure. Startup and listener failures
return an error string, including the failing operation. An occupied port is a
startup failure. Individual connection failures close that connection and allow
the next request. The function has no normal successful return or graceful-stop
operation.

`Request` exposes `method`, `path`, and `query` as strings. Accepted requests have
method `GET`. The path keeps percent escapes exactly as received, and ends before
the first `?`. The query excludes that separator and remains encoded. Neither
field is decoded or normalized. Mutating the request changes only the handler's
local request object.

`http.text(status: Int, body: String): Response` constructs a response with an
explicit status and UTF-8 body. Responses use `text/plain; charset=utf-8`, a byte
`Content-Length`, and `Connection: close`. Status 204 and 304 omit the body and
content length; status 205 sends an empty body. Statuses outside 200–599 or bodies
larger than 1 MiB produce status 500 instead of the supplied response.

## Protocol and limits

The server implements a restricted HTTP/1.1 origin-form request protocol:

- One bodyless GET request per connection, processed sequentially.
- CRLF line endings and ASCII request lines and header values.
- Exactly one nonempty Host header. Header names are case-insensitive.
- Content-Length may be absent or a single decimal zero value.
- Request targets start with `/` and contain valid URI characters and percent escapes.
- The request line, headers, and final blank line occupy at most 8192 bytes.
- One absolute two-second header-read deadline, including fragmented input.
- One absolute two-second response-write deadline, including partial writes.

Malformed requests and unsupported body framing receive 400. Unsupported methods
receive 405 with `Allow: GET`; unsupported versions receive 505; Expect receives
417; oversized headers receive 431; incomplete headers that exceed the deadline
receive 408 when the connection remains writable. Responses to HEAD omit a body.
EOF and socket failures close the connection. Extra requests on the same connection
are not dispatched. Request bodies, keep-alive, routing, TLS, HTTP/2, streaming,
and concurrent handlers are outside this API.

The handler runs synchronously without an execution deadline. Fatal panic or
forced process termination does not unwind application scopes; the operating
system reclaims the process's sockets. On supported returns and I/O error paths,
the Neri library closes each accepted descriptor explicitly.

## Implementation boundary

Protocol parsing, response construction, retries, deadlines, and descriptor
ownership are implemented in [Neri](../stdlib/http.hk). A small
[C adapter](../native/runtime/socket.c) supplies platform socket layouts,
constants, monotonic time, and individual system calls. These unsafe imports
require runtime ABI 1.7 and the sockets feature. The toolchain includes and
checksums the standard-library source alongside its native artifacts.
