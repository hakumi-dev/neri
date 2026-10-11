# Asynchronous I/O and navigation loading

Typed asynchronous materialization owns an `AsyncQuery<T>` resource. Starting
compiles SQL and binds parameters; each `poll()` returns pending socket interest
or a completed typed query result. A pending socket `-1` means local work remains.
Only a completed successful materialization publishes rows and resolves them
through the context's identity tracker. A cancelled, failed, or disposed operation
preserves structured failure and cleanup details. Cancellation tokens belong to
one sequential operation; they are not thread-safe.

The PostgreSQL adapter implements nonblocking connection, read, write and transaction polling through
libpq. SQLite uses `SQLiteAsyncSession.open(path)` and its `SQLiteAsyncProvider`:
one isolated native worker performs SQLite database I/O in its own managed heap.
Session startup waits for worker readiness before returning and opens no database.
The caller's polls use a zero-time worker completion check. VM progress and lock
wait callbacks observe worker cancellation and the operation's monotonic deadline.

SQLite messages copy values and diagnostics across heaps using a private bounded
wire protocol. Requests and responses are limited to 32 MiB; buffered responses
also have a 100,000-row bound. Exceeding a transport bound fails explicitly. Each
session retains one writable connection on its worker, so reads and writes share
the same transaction lease. The connection opens during the first queued job.
Disposing an unfinished read requests cancellation; the session retains the
worker until cancellation completes or session close joins its cleanup. Ordinary
SQLite providers retain their synchronous contract and explicitly reject async
read, write and transaction starts. Use the asynchronous session for this execution surface.

`startWriteAsync` owns an atomic batch operation; provider `beginWriteAsync`
returns its ordinary driver for state machines such as context saves. Each adapter
freezes parameters and result metadata before execution. BEGIN or a recovery
savepoint precedes commands; affected counts and typed RETURNING values are
checked before a caller-supplied validation callback runs on the calling heap.
Only successful validation permits COMMIT or RELEASE. `startControlAsync` owns
BEGIN, COMMIT, ROLLBACK and savepoint operations; transaction leases and savepoint
IDs are provider-issued and sequential.

Cancelling a write before its final commit boundary aborts PostgreSQL's connection
or cancels SQLite's worker job and polls rollback to completion. Once COMMIT or
final RELEASE is queued, polling preserves its actual outcome; late cancellation
does not replace an acknowledged commit with a cancellation result. Recovery
ignores the cancelled caller token and expired caller deadline. Disposing an
unfinished SQLite operation requests cancellation and retains pending cleanup on
its provider. Poll `pollCleanup()` or `pollControlCleanup()` before reusing it;
the session rejects admission until cleanup finishes. Deferred write finalizers
run after rollback reaches its terminal state. Context save operations retain
their own cleanup state and expose `pollSaveCleanup()`.

Operation and context polling/close do not join native workers. Final
`SQLiteAsyncSession` disposal cancels and joins its worker, closes its owned
connection and reports cleanup failures, following the standard workers resource
contract. Dispose active operations and complete pending rollback before closing
the session. Asynchronous set-based writes and migration execution are currently
explicitly unsupported; tracked batches, context saves and transaction control
use the asynchronous surface.

Stable, context-owned `CollectionNavigation` objects record Unknown, Loading,
Partial, and Loaded state per entity. Complete collection and reference loads mark
Loaded; filtered collection loads mark Partial. Chained load journals restore
loaded metadata with navigation values after later-stage failure. Changing a
reference foreign key or principal key invalidates its corresponding cached state.
Removed or detached tracked instances fail cached access instead of returning
stale values.

Register each stable relationship once with `registerNavigation(principals,
dependents, relation)`. Materialization of either root then fixes references and
inverse collection membership against all persisted tracked instances. Identity
resolution preserves local scalar changes, and fixup preserves pending foreign-key
or contradictory local reference edits. Fixup establishes known membership; it
does not claim that a partially observed collection is fully loaded.

`lazyCollection` and `lazyReference` perform a complete load on first access,
return cached typed rows after Loaded state, and reject recursive loading. A
Partial collection triggers a complete load. A failed load restores its previous
metadata and keeps the error observable. These accessors perform explicit work;
generated entity field access itself remains an ordinary Neri field read.
