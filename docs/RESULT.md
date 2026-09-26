# Result

`result::Result<T, E>` is a closed outcome with `Ok(value)` and `Error(error)`.
`result::Failure` owns a stable code, operation, detail, transferred-byte count,
operating-system code, primary cause, and secondary close failure. Adapters preserve the existing
optional and Boolean APIs and expose additive `*Outcome` functions.

Use exhaustive `match` to handle each outcome, or `try expression` to extract a
success value and return a compatible error from the enclosing function. The
function's return type continues to describe its possible failure. See
[typed propagation](TRY.md) for the language contract and library opt-in.
`map`, `flatMap`, and
`mapError` transform success, chain dependent operations, and transform errors.

Adapters cover file reads, HTTP request parsing/header reads/writes/listeners,
and crypto digest, hex, and random-byte operations. `files::ReadFailure` retains
the root-path component and secondary close failure. `http::ParseFailure` retains
the parser status and HEAD flag; `http::HeaderFailure` retains the transport
status and bytes read. A failed write reports bytes transferred before the
failure. Listener success means the listener stopped normally.
