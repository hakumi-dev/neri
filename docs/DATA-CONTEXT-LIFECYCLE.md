# Context lifecycle

`ChangeContext.addSaveInterceptor(interceptor)` registers a `SaveInterceptor`
in registration order. `beforeSave(context)` runs before write planning and
can return a structured failure to veto database execution. Entity field edits
made by that callback participate in the save. `afterSave(context, result)`
observes the final synchronous or asynchronous outcome after tracking recovery
has completed. It cannot replace an acknowledged database outcome. A veto does
not start an operation and does not invoke completion observers.

Interceptors cannot recursively save, start another context operation, or change
interceptor registration while a callback is executing. Interceptors remain
registered across context resets. Application-owned state in an interceptor must
be reset by the application's pool reset callback when necessary.

`ContextPool<T>(factory, reset, capacity)` provides sequential, bounded reuse of
ordinary context objects. `acquire()` returns an owned `ContextLease<T>`;
`context()` returns its context while the lease is open. Closing the lease calls
the configured `reset(context)` exactly once. A reset failure discards that
context and remains visible as a resource close failure. A pool with active
leases cannot close. Pool operations are sequential; the pool is not thread-safe.

The reset callback returns `WriteFailure?`. Generated contexts expose
`resetForPool()` to reject unfinished operations and active or uncertain
transactions, clear tracked identities and write links, and invalidate navigation
load states. Reset application-specific tenant, filter, interceptor, and other
mutable state in the application callback as well. A pool does not own borrowed
provider sessions; applications keep those sessions alive until every lease and
operation is finished.

Disposed resources do not revoke ordinary context references previously obtained
by callers. Such references are borrowed for the lease's lifetime and must not
be used after the lease is returned. Context pooling does not permit concurrent
operations on a connection or change its transaction isolation.

`SaveExecutionStrategy(maximumRetries)` wraps a tracked synchronous or async save
with bounded replay after PostgreSQL serialization/deadlock failures or SQLite
contention. An attempt must finish rollback and leave the context reusable before
another attempt starts. Cleanup failures, ambiguous connection/commit outcomes,
concurrency conflicts and explicit application transactions are returned directly.
The async strategy uses one overall deadline and cancellation token across attempts;
it starts context admission and snapshot capture when the operation is created.
Attempts run sequentially, with a local pending poll between retries. Applications
must keep tracked entity values stable until the whole operation finishes, because
a retry captures a new snapshot. Save interceptors run for each attempt.

`SaveExecutionStrategy` accepts `initialBackoffMilliseconds`,
`maximumBackoffMilliseconds`, and an optional `prepareRetry(context, attempt)`
callback. Delay grows exponentially within the configured bound. The callback runs
only after confirmed rollback and cleanup. Concrete providers expose
`reconnectAfterRollback()` for idle owned sessions; SQLite requires a retained
file-backed database path. Async delays remain cancellable local pending polls.
An uncertain commit outcome is returned without replay.

`ChangeContext.enlistTransaction(lease)` enrolls an active provider transaction
owned by another caller. Saving uses that lease and savepoints. The external owner
completes the transaction, then each enrolled context calls
`synchronizeTransaction()` to publish committed baselines or restore rolled-back
entities, generated values and relationship keys. An active or unacknowledged
native outcome cannot finalize tracker state.

`SQLiteProvider.borrowNativeTransaction(handle)` and
`PostgreSQLProvider.borrowNativeTransaction(handle)` create sequential borrowed
wrappers around an active native transaction. The caller retains the connection
and controls outer transaction completion. `nativeTransactionLease()` exposes the
wrapper lease. After native completion, `acknowledgeNativeTransaction(committed)`
verifies that the native transaction has exited and records the owner's outcome.
`enlistNativeTransaction()` reuses the wrapper after the owner starts another
transaction. Closing a completed wrapper preserves the caller's native handle.

`InterceptedProvider(provider, interceptor)` applies public `CommandInterceptor`
callbacks to trusted query plans, write batches, set writes and streaming plans.
Applications build replacement write batches
with `WriteBatch.add` and bound `WriteValues.add`; SQL text is application-authored
and follows the underlying provider's command validation. Leased writes require
an active provider transaction before replacement or suppression callbacks run.
Returning null preserves the incoming command; returning a plan replaces it, and returning
a result suppresses execution. Wrappers compose in registration order. Async
replacement results own their values and honor cancellation, deadlines and internal
navigation row bounds. Interceptors cannot recursively enter their wrapper.

Save admission validates tracked primary and alternate identities before graph
change detection. `registerChangeDetector(detector)` runs detection before save
interceptors. Each detector receives a `GraphJournal`; tracker membership, identity,
state and graph edges roll back atomically when detection fails. Detectors journal
application navigation arrays and entity field assignments before changing them.
