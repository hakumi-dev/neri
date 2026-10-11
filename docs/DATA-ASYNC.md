# Asynchronous relational operations

Neri Data exposes owned polling operations. Each poll returns a completed result
or `Pending(socket, interest)`. A socket of `-1` requests local rescheduling;
other sockets can be registered with an event loop. Polling never waits for
network or database I/O. PostgreSQL sends commands through nonblocking libpq;
SQLite runs database work on the session's isolated worker heap.

`Query<T>.executeDeleteAsync`, `executeUpdateAsync` and `executeUpdateSetAsync`
return `Result<AsyncWrite, WriteFailure>`. Set writes report the actual affected
row count, including zero. They preserve tracked instances and original
snapshots. A query bound to a change context uses that context's active lease
and admission guard. Each set operation has an implicit transaction or a
savepoint inside an existing transaction. Failure rolls back the operation
before the session is reused. Parameters are copied when the operation starts.

`SQLiteAsyncProvider.migrateAsync` and `PostgreSQLProvider.migrateAsync` return
`Result<AsyncMigration, MigrationFailure>`. `Provider.beginMigrationAsync`
provides the ordinary driver contract. Migration operations validate the
catalog, applied signatures and contiguous history prefix, execute forward or
reverse operations, update stamps, and commit as one transaction. The target
is a migration identifier, an empty string for an empty schema history, or
`null` for the full catalog. Catalogs contain at most 1000 migrations.
SQLite copies the complete schema catalog, including rebuild metadata and
foreign-key tuples, to its worker; its bounded wire accepts at most 1000
operations, fields, indexes or tuple entries in each list and at most 32 MiB
per message. PostgreSQL holds an advisory transaction lock while inspecting
history and changing schema. A committed result remains the completion outcome
when cancellation arrives at the commit boundary.

`InsertData`, `UpdateData` and `DeleteData` seed operations use copied bound
parameters and execute in catalog order alongside schema operations. Each seed
write affects exactly one row; a missing or ambiguous key rolls back the
migration with a drift failure. Seed values contribute to applied signatures,
and repeating an applied catalog leaves its data unchanged. Explicit keys are
non-null, floating values are finite, and binary snapshots retain their bytes.

`IncludedQuery<P,D>.allAsync` and `loadAsync` return
`Result<AsyncNavigation<P,D>, QueryFailure>`. `ReferencedQuery<P,D>` exposes the
same terminals with `AsyncNavigation<D,P>`. They collect roots, request related
rows in batches of at most 50 keys, validate identities and local relationship
state, and publish collection/reference state after all related reads succeed.
Navigation loaders require the context's registered tracking query roots;
detached and isolated no-tracking query modes do not load relationship graphs.
Filtered collection loading retains the same per-principal ordering and
pagination contract as synchronous loading. Root and related result sets are
bounded by their navigation limits. Cancellation or a failed related read
preserves previously published navigations. The timeout covers the complete
coordinated load. Root tracking keeps the query's ordinary identity resolution
and registered materialization fixup behavior.

Related materialization fixup, navigation arrays, references and loaded states
are journalled before publication. A later validation or path-stage failure
restores their prior state. Identity resolution retains newly tracked entities;
subsequent ordinary queries can apply their registered fixup again.
The `all` and `allAsync` terminals also checkpoint known navigation state before
the root query, so root materialization fixup participates in failure recovery.

`lazyCollectionAsync` and `lazyReferenceAsync` return owned `AsyncLoad<T>`
operations. A loaded navigation completes from tracked memory. Unknown and
partial navigation states use the asynchronous coordinator. While a lazy
load is pending its state is `Loading`; failure, cancellation and abandonment
restore the previous state. Ordinary entity field access remains local.

Dispose owned operations with `using`. Closing an unfinished PostgreSQL
operation aborts its connection. Before COMMIT is sent, this rolls back its
transaction. Closing during COMMIT reports an unknown commit outcome; the
caller must reconcile database state before retrying. COMMIT responses are
drained despite cancellation or deadline expiry after COMMIT starts. SQLite
cancels the worker job and prevents reuse until worker cleanup completes.
`StreamOptions` supplies cancellation and an optional deadline for each
operation. Completed operations can be polled repeatedly and return the same
result.
