# Context lifecycle

`ChangeContext.addSaveInterceptor(interceptor)` registers a `SaveInterceptor`
in registration order. `beforeSave(context)` runs before snapshot capture and
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
a retry captures a new snapshot. Save interceptors run for each attempt. The strategy
has no reconnect or commit-verification callback and no automatic backoff policy.
