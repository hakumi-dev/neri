# PostgreSQL provider

`providers/postgresql/manifest.json` exports the `postgresql` library in namespace
`neri_data_postgresql`. It links the platform's `libpq` C ABI and uses APIs present
in libpq 14 and later. `PostgreSQLSession.open(conninfo)` owns a sequential
connection; its `provider()` is borrowed by contexts and queries. Close the session
after its contexts and active operations have finished.

Connection strings supply an explicit, nonempty `hostaddr`. The adapter parses
the actual libpq options and rejects empty addresses in a multi-host list. This
keeps DNS lookup outside nonblocking connection polling. Other libpq connection
options, including TLS verification, authentication, host names, and ports, remain
application configuration. Startup selects UTF8 client encoding and verifies the
server's negotiated encoding.

`PostgreSQLConnect.start` returns an owned connecting operation. Its `poll()`
returns a pending socket and read/write interest, an opened provider, or a
structured failure. Pending socket `-1` requests immediate local rescheduling.
The opened provider takes the native handle; closing the connection operation
afterward preserves that transferred provider.

Reads bind every parameter through `PQsendQueryParams`, flush output in
nonblocking mode, consume available input, and obtain results only after
`PQisBusy` reports readiness. Single-row mode copies each row before clearing its
native result. Integer decoding checks PostgreSQL integer OIDs and Int range;
Bool accepts PostgreSQL bool, Float accepts finite float4/float8, String accepts
text/varchar/char, and Binary accepts bytea. Required NULL values and mismatched
column names, counts, types, or encodings fail explicitly. Decimal mappings retain
their canonical exact decimal representation in TEXT.
Binary decoding accepts both hexadecimal and legacy octal escape output,
including empty bytea and embedded zero bytes.

Transactions use provider-issued leases. Savepoints require the latest active
savepoint ID and its owning lease. Atomic batches validate affected-row counts
and typed RETURNING rows before committing. A batch inside an explicit transaction
uses a recovery savepoint. SQLSTATE, operation, native diagnostic message, and
cleanup causes remain structured. The provider never retries a failed transaction.
`beginWriteAsync` and `beginControlAsync` use native send/flush/consume polling
without waiting; returned-value validation runs before the final commit boundary.
Cancellation after that boundary preserves the command's actual outcome. Driver
polls copy and clear each native result, and abort the owned connection if an
unsupported COPY response changes the wire protocol.

Migrations take a transaction-scoped advisory lock, validate the catalog history
prefix and signatures, apply the requested target, and commit schema and stamps
together. `migrationHistory` is read-only outside a lease and takes the same lock
inside a caller-owned transaction. Initial tables are created before deferred
foreign-key constraints, including cycles; migration schema scripts drain all
libpq results.

Cancelling or disposing an unfinished native read closes its connection and marks
an active lease Failed. It calls no blocking libpq cancellation API. The server
rolls back an interrupted session's transaction; applications create a new session
before retrying. Successful reads drain the complete protocol and permit reuse.
Synchronous terminals drive the same native operations while waiting for socket
readiness; asynchronous polling performs no database I/O wait. Checked arithmetic
expressions remain explicitly unsupported by this provider.

The executable `contract` unit requires `NERI_POSTGRES_CONNINFO` and a disposable
database accessible to that connection. Its temporary tables remain connection
local. Set the platform library search path to the libpq installation when needed.

The driver follows PostgreSQL's [asynchronous command processing](https://www.postgresql.org/docs/current/libpq-async.html),
[connection control](https://www.postgresql.org/docs/current/libpq-connect.html),
and [single-row results](https://www.postgresql.org/docs/current/libpq-single-row-mode.html).
