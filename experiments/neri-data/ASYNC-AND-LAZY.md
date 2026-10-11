# Asynchronous reads and navigation loading

Typed asynchronous materialization owns an `AsyncQuery<T>` resource. Starting
compiles SQL and binds parameters; each `poll()` returns pending socket interest
or a completed typed query result. A pending socket `-1` means local work remains.
Only a completed successful materialization publishes rows and resolves them
through the context's identity tracker. A cancelled, failed, or disposed operation
preserves structured failure and cleanup details. Cancellation tokens belong to
one sequential operation; they are not thread-safe.

The PostgreSQL adapter implements nonblocking connection and read polling through
libpq. SQLite uses `SQLiteAsyncSession.open(path)` and its `SQLiteAsyncProvider`:
one isolated native worker performs SQLite database I/O in its own managed heap.
Session startup waits for worker readiness before returning and opens no database.
The caller's polls use a zero-time worker completion check. VM progress and lock
wait callbacks observe worker cancellation and the operation's monotonic deadline.

SQLite messages copy values and diagnostics across heaps using a private bounded
wire protocol. Requests and responses are limited to 32 MiB; buffered responses
also have a 100,000-row bound. Exceeding a transport bound fails explicitly. Each
standalone read opens and closes its database on the worker before replying.
Disposing an unfinished read requests cancellation; the session retains the
worker until cancellation completes or session close joins its cleanup. Ordinary
SQLite providers retain their synchronous contract and explicitly reject async
read starts. Use the asynchronous session for this execution surface.

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
