# Provider architecture and verified references

Neri Data separates typed query construction, SQL planning, provider execution,
and entity decoding. Its first concrete provider reads and writes SQLite databases.
The generated `Database` accepts a borrowed provider, so application queries use
`db.customers.where(active: true).take(20).all()` without repeating connection
selection. Explicit `all(provider)` remains available.

The context tracks mapped scalar snapshots and one entity instance per key.
Its `saveChanges()` submits inserts, changed-column updates, and deletes as one
atomic provider batch. SQLite writes require an explicit writable open. There
are no lazy loading or automatic connection pooling. Typed
[joins and compositional projections](JOINS-AND-PROJECTIONS.md) produce
untracked query results. Typed
[migrations and schema inspection](MIGRATIONS.md) expose SQLite schema lifecycle
operations separately from tracked entity writes. The
context borrows its provider: the caller closes it after all operations.
`SQLiteSession.open(path)`, `openWritable(path)`, and `createWritable(path)`
return an owned resource for read-only, existing writable, and creatable writable
databases respectively. `session.provider()` borrows the underlying
`SQLiteProvider`. Keep the session and its aliases exclusive to one worker,
perform operations sequentially, and create a generated context per request.
The session owns the physical connection across those requests; each request
owns its explicit transaction and rolls it back when the request fails or is
cancelled. Close the session after its contexts and operations finish.

Use `using` to close the session on scope exit. Its `resources::Outcome`
preserves the primary failure and records cleanup failures separately in
`closeFailures`. `SQLiteSession.close()` returns a failure with operation
`sqlite.session.close` when the provider cannot close; it retains the provider
so an explicit close can be retried. Successful close is idempotent and rolls
back an active transaction. A borrowed provider alias remains an ordinary
object after successful close, but operations reject the closed state without
accessing the released SQLite handle.

Concurrent workers require SQLite configured in multi-thread or serialized mode;
single-thread builds are unsuitable even when every worker owns a different
connection. This follows SQLite's [threading contract](https://sqlite.org/threadsafe.html).
The provider starts write transactions with `BEGIN IMMEDIATE`. Another writer
causes an immediate `WriteFailure.Transaction`; the adapter installs no busy
handler or automatic retry. SQLite permits only one active writer. A failed
commit triggers rollback and state reconciliation, so repeating a commit or an
entire request blindly is unsafe. See SQLite's
[transaction rules](https://sqlite.org/lang_transaction.html).

Streaming deadlines interrupt SQLite through its
[progress callback](https://sqlite.org/c3ref/progress_handler.html), which is
removed before returning. Cancellation of a read does not imply cancellation of
prior writes in the transaction: explicitly roll back, or let a request-owned
`SQLiteSession` leave its `using` scope. `CancellationToken` is local to a
synchronous call; it does not provide cross-worker interruption.

Mapped Float values use native double binding and strict REAL decoding;
see [FLOATS.md](FLOATS.md) for finite values, NULL, and affinity rules.
Connections and contexts are used sequentially. See
[streaming and cancellation](STREAMING.md) for scoped row visitors and deadlines,
[navigation loading](NAVIGATION-LOADING.md) for batched collection and inverse
reference reads, [reference loading](REFERENCE-LOADING.md) for dependent-root
queries and nullable foreign keys, and
[query composition and tracked changes](QUERY-AND-CHANGES.md) for transaction,
conflict, identity, and operator-order contracts and their verified references.

## Research basis

The references below were checked against author-hosted papers and official
documentation on 2026-09-22. The engineering choices are Neri Data decisions;
the papers do not prove correctness of this implementation.

| Verified source | Relevant result or contract | Neri Data application |
| --- | --- | --- |
| Meijer, Beckman, Bierman, [*LINQ: Reconciling Objects, Relations and XML in the .NET Framework*](https://gavinbierman.github.io/assets/pdf/sigmod2006.pdf), SIGMOD 2006, p. 706 | Composable query operators and expression representations connect host-language queries to relational execution and object materialization. | Keep the existing typed `Query<T>` surface, explicit plan terminal, and generated decoders. Binding a provider does not move SQL knowledge into entity classes. |
| Cheney, Lindley, Wadler, [*A Practical Theory of Language-Integrated Query*](https://homepages.inf.ed.ac.uk/jcheney/publications/cheney13icfp.pdf), ICFP 2013, §§1–2, [DOI](https://doi.org/10.1145/2500365.2500586) | Quotation and normalization support compositional queries; their calculus characterizes translation to a single SQL query. | One supported read terminal executes one compiled statement. Unsupported quotations remain explicit errors. Neri does not implement the paper's complete calculus or inherit its theorem; general normalization and nested queries are outside this contract. |
| Microsoft, [EF Core database providers](https://learn.microsoft.com/en-us/ef/core/providers/) and [context configuration](https://learn.microsoft.com/en-us/ef/core/dbcontext-configuration/) | Providers are separate libraries selected by context configuration; context lifetime and concurrent use require explicit care. | Inject a provider into the generated context. Keep SQLite imports in a separate project unit. Borrowed provider ownership and the subset of tracked-change behavior are explicit. |
| Microsoft, [client versus server evaluation](https://learn.microsoft.com/en-us/ef/core/querying/client-eval) and [query null semantics](https://learn.microsoft.com/en-us/ef/core/querying/null-comparisons) | Unsupported filtering translation can fail instead of silently executing client-side; SQL NULL needs deliberate treatment when translating host-language comparisons. | Preserve the existing translation failures and nullable equality compensation. The adapter executes the plan and does not evaluate host predicates or rewrite NULL comparisons. |
| SQLite, [parameter binding](https://www.sqlite.org/c3ref/bind_blob.html) | Binding uses positional indexes; `SQLITE_STATIC` requires buffers to remain alive until rebinding or statement finalization. | Bind scalar values separately from SQL and retain native text buffers through finalization. SQL-looking strings remain parameter values. |
| SQLite, [result access](https://www.sqlite.org/c3ref/column_blob.html) and [datatypes](https://www.sqlite.org/datatype3.html) | Column accessors can coerce values; text pointers have limited lifetime; booleans use integer storage. | Inspect storage type before extraction, accept only 0/1 for mapped booleans, preserve NULL, copy UTF-8 bytes before stepping/finalizing, and reject unsupported cells. |
| SQLite, [prepare](https://www.sqlite.org/c3ref/prepare.html), [open](https://www.sqlite.org/c3ref/open.html), [close](https://www.sqlite.org/c3ref/close.html), and [statement read-only status](https://www.sqlite.org/c3ref/stmt_readonly.html) | Preparation consumes one statement; even a failed open can need cleanup; unfinalized statements can prevent close. Read-only statement classification is not a sandbox for arbitrary SQL/functions. | Open an existing database read-only, finalize on every execution path, copy errors while their owner is alive, and reject use after close. Execute generated plans, not arbitrary user SQL. |

## Implementation reference: Hakumi ORM

The local `hakumi-orm` checkout at
`791117ac18c513088495ff341d5f08b2a3c2b974` provides a useful implementation
comparison: `lib/hakumi_orm/adapter/base.rb` separates parameterized execution and
connection lifetime; `dialect.rb` owns SQL syntax differences; `relation.rb`
returns new query relations; `adapter/sqlite.rb` keeps SQLite details local.

Neri keeps those boundaries while using a context-oriented public API. Hakumi's
transaction nesting, statement cache, adapter registry, and preloads are not
dependencies of this adapter. Changes are collected by a generated context.

## Extension contract

Another relational provider implements `Provider.dialect()`,
`Provider.supports(...)`, and `Provider.execute(readonly SqlPlan)` in its own
project unit. It receives SQL, ordered typed parameters, a row limit, and result
column metadata. It returns named `RawRow` cells or a failure; the shared runtime
checks limits and invokes the generated decoder. A driver does not need to
understand quotations or generated entity constructors.

Streaming is a separate capability. A provider advertises `StreamingReads` and
overrides `stream(readonly StreamPlan, fn(RawRow): Bool)`. The plan carries an
optional maximum row count, monotonic deadline, and cooperative token. The
provider delivers copied rows sequentially, honors callback stop, releases its
native resources before returning, and reports completion, stopping,
cancellation, or failure with a delivered-row count. The default implementation
rejects streaming rather than buffering through `execute`.

`WindowFunctions()` advertises `ROW_NUMBER() OVER (PARTITION BY ... ORDER BY ...)`
for per-parent navigation pages. The loader also requires streaming and, when
its expressions need them, checked scalar operations. SQLite caches the result
of a version check and a window-function probe when opening the connection;
closed or poisoned connections no longer advertise this capability. A
placeholder dialect alone does not establish window-function support.

Write batches contain ordered generated SQL commands, typed `Cell` parameters,
and expected affected-row counts. A writable provider overrides
`Provider.writeBatch(readonly WriteBatch)` and must commit the entire batch or
roll it back. The default implementation returns a structured unsupported failure.
Successful results report the affected-row count. The shared change context
advances its snapshots only after matching successful completion.

Explicit transactions use `beginTransaction`, `commitTransaction(lease)`,
`rollbackTransaction(lease)`, `transactionState(lease)`, and
`writeBatchInTransaction(batch, lease)`. Begin returns a provider-issued integer
lease; all other write owners are rejected while it is active. A leased batch
uses an internal savepoint and advances tracker baselines provisionally.
The context journals those baselines until commit or verified rollback.
Provider transaction states distinguish active, committed, rolled back,
invalid ownership, and uncertain failure. Default implementations reject this
capability, keeping read-only providers compatible.

`Savepoints()` adds lease-bound creation, rollback-to, and release operations.
SQLite supports nested owned savepoints with strict LIFO validation. Graph saves
use a separate per-save tracker checkpoint to recover without losing earlier
work in the outer transaction. See [savepoint recovery](SAVEPOINTS.md).

Generated integer-key batches request result-column metadata on individual
commands and return aligned `WriteReturnedRows` through `SavedWithValues`.
The context validates those values within a lease before committing a standalone
save. See [generated values](GENERATED-VALUES.md) for key and rollback rules.

The current dialect enum distinguishes question-mark and PostgreSQL-style
placeholders. Placeholder rendering alone is not a PostgreSQL adapter or a
claim of portability across every SQL engine. Provider-specific behavior must
be covered against that engine when it is implemented. New capabilities need
an explicit shared contract rather than SQLite types in the runtime.

## Validation obligations

The tests exercise provider binding through immutable query transformations and
projections, missing-provider failure, explicit overrides, and the standalone
plan API. SQLite tests exercise real files, parameter binding, strict scalar
decoding, NULL, errors, and connection cleanup. Integration contracts use the
generated `Customer` and `Order` mappings for queries, tracked changes,
rollback/retry, optimistic conflicts, and reopening the file.

These are behavioral checks, not a formal proof or a production performance
claim. Test-only fixtures and the production migration API create isolated
test schemas; application writes use generated
parameterized commands from the change tracker.
