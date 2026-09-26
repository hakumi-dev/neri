# Set writes and provider diagnostics

`Query<T>.executeUpdate(labels changes: fields of T)` assigns constant values to
mapped scalar fields. `executeUpdateSet(setters)` assigns quoted scalar
expressions, and `executeDelete()` removes the selected rows. These operations
execute immediately and return `WriteResult.Saved(affectedRows)`, including zero. They
select keys in SQL and do not fetch entities or invoke the entity decoder.

```neri
let changed = db.customers.where(active: false).executeUpdate(active: true)
let cleared = db.customers.where(id: 42).executeUpdate(nickname: null)
let removed = db.customers.where(active: false).executeDelete()
```

Field labels and their Neri types are checked at compilation. Runtime mapping
validation rejects unmapped fields, empty updates, and primary-key assignments.
Required scalar fields reject null; optional fields accept it. Values are bound
separately from SQL, including text containing quotes. The mapping declares the
primary-key column independently of the change tracker. Existing mappings with
no key metadata continue to support reads; set writes require key metadata.

## Computed assignments

`updateSet<T>()` creates an immutable assignment builder. Each
`.set<V>(target: quote fn(T): V, value: quote fn(T): V)` returns a new builder,
so different scalar types can be combined without erasing their type checks.

```neri
let increment = 5
let target = quote do |order: Order|: Int
  return order.total
end
let value = quote do |order: Order|: Int
  return order.total + increment
end
let setters = updateSet<Order>().set(target, value)
let changed = db.orders.where(customerId: 10).executeUpdateSet(setters)
```

Targets must be direct mapped non-key scalar fields. Empty builders, duplicate
target columns, unsupported expressions and mapping/type mismatches fail before
provider dispatch. A value quotation's signature matches its target; a quoted
optional result can contain a required value of the same scalar type or NULL.
Optional-to-required assignment and implicit numeric conversion are rejected.
The constant field-label API remains available for simple assignments.

All right-hand sides refer to the old values of their row. Chaining two setters
does not make the second see the first's result: `a = b, b = a` swaps the values.
The SQL statement binds target-selection parameters first, then assignment
parameters in builder order. Target keys remain materialized before modification.

Arithmetic and text operations reuse the
[checked scalar expression contract](COMPUTED-EXPRESSIONS.md). Every assignment
also validates its final result inside SQLite: INTEGER for Int, finite REAL for
Float, INTEGER 0/1 for Bool, and TEXT for String. NULL is accepted only for
optional targets. This final validation is necessary even for a direct column
copy, because a set write does not pass through a typed row decoder. A malformed
stored value or arithmetic failure aborts the operation and uses the same
transaction/savepoint recovery as other set writes.

Computed setters require `CheckedScalarExpressions` as well as `SetWrites`,
checked before a write or transaction dispatch. Their current execution and
translation contract is SQLite; PostgreSQL rendering rejects this API until an
equivalent checked implementation exists. Constant setters retain their existing
placeholder-dialect support. Navigation access, subqueries and aggregates inside
setters remain outside the scalar expression contract.

## Selection and tracking

The complete query pipeline selects target keys, preserving filters after a
limit, ordering, secondary ordering, offset, and distinct stages. The target
keys are materialized inside the database before modification. Unbounded set
writes have no implicit 1000-row cap; an explicit `take` retains its normal
query limit. PostgreSQL placeholder ordering is covered by compilation tests;
the real execution adapter is SQLite. Set writes require SQLite 3.35 or later
for the materialized target-key selection.

Set writes leave tracked objects and snapshots unchanged. A later tracked save
still checks its original scalar snapshot, so a conflicting bulk modification
can produce `WriteFailure.Concurrency`. Callers explicitly refresh or clear
their tracked state when appropriate. Set writes do not infer concurrency
conditions: include them in the predicate and inspect the affected-row count.

## Transactions

A generated query retains its owning context through query composition. Within
an explicit context transaction, set writes use that context's lease and a
savepoint. A failed statement rolls back its savepoint, including partial
changes under SQLite's `FAIL` conflict mode, while earlier operations remain
pending. An automatic database rollback restores the context's tracker journal.
Another context sharing the connection cannot issue an unleased write while
that lease is active.

Standalone SQLite set writes use an explicit transaction around one statement,
including binding, stepping, finalization, and commit failure handling. Multiple
standalone calls remain separate transactions.

## Diagnostic observer

`DiagnosedProvider(provider, observer)` wraps a provider and forwards its results.
The observer receives a readonly `ProviderDiagnostic` after each read, stream, tracked
write batch, set write, transaction, or savepoint operation. The event reports operation
name, elapsed monotonic milliseconds, statement/parameter counts, affected rows,
returned rows, and a failure category. An unavailable elapsed time is `-1`.
Stream events are emitted after native cursor cleanup. Their returned count is
the provider's raw-row delivery count; the typed terminal separately counts
successfully decoded visitor deliveries. A decoding failure can therefore
produce a provider stop event with one additional raw row. The typed sink also
uses `false` for cancellation observed around a visitor, so that case can emit
a provider stop event while the query returns `Cancelled`. The stream event
uses `failureKind` values `stopped` and `cancelled` for those outcomes; intentional
stopping is not an execution error.
Counts describe submitted plans and batch commands; internal transaction-control
SQL is not enumerated. Savepoint creation, rollback-to, and release are forwarded
with their lease and identifier; their events contain no SQL or parameter values.
`SetWrites`, `TrackedWrites`, `ExplicitTransactions`, and `Savepoints`
capabilities report current SQLite write availability, alongside typed reads.

SQL text is available for a read or set statement. A tracked batch includes its
submitted statements separated by newlines, retaining parameter placeholders.
Its duration is measured once for the provider operation, not per statement;
a failed batch can contain statements that the provider never reached.
Returned-row counts exclude empty output
slots in mixed batches. Events expose neither parameter values nor transaction
leases. Provider error messages are omitted because they may contain data;
the original error remains in the caller's unchanged result. Observers must
return normally. Reentrant reads and writes through the same wrapper fail
before reaching the underlying provider. Row callbacks receive the same
reentrancy protection; capability and transaction-state
inspection remain available.

These are completed-operation events at the provider boundary. They do not
report client-side decoding failures, intercepted queries, distributed traces,
or asynchronous execution. Computed setters use the same completed-operation
events and redact bound values in the same way as constant assignments.

## Verification

```sh
scripts/neri.sh run --project experiments/neri-data --unit bulk-contract
scripts/neri.sh run --project experiments/neri-data --unit diagnostics-contract
scripts/neri.sh run --project experiments/neri-data/provider-contract --unit bulk
scripts/neri.sh run --project experiments/neri-data/provider-contract --unit bulk --release
scripts/neri.sh run --project experiments/neri-data/provider-contract --unit floats
```

The SQLite integration covers more than 1000 affected rows, composed selection,
zero-row results, bound text and null values, key-update rejection, unchanged
tracked objects, savepoint recovery after partial statement failure, outer
rollback/commit, foreign-context lease rejection, and automatic rollback after
an earlier tracked save. Computed assignments cover old-row swaps, mixed scalar
types, optional values, parameter order, invalid targets, checked arithmetic
failure and direct-copy storage validation. Diagnostic contracts exercise result forwarding,
redaction, mixed returned rows, savepoint forwarding, and reentrant transaction
and savepoint rejection.

## Verified primary references

- E. F. Codd, [A Relational Model of Data for Large Shared Data Banks](https://www.seas.upenn.edu/~zives/03f/cis550/codd.pdf),
  *Communications of the ACM* 13(6), 377–387, 1970. The original paper's relational
  model and data-independence discussion support the collection-oriented design.
  It is a conceptual foundation, not a specification of these mutation APIs.
- Microsoft, [ExecuteUpdate and ExecuteDelete](https://learn.microsoft.com/en-us/ef/core/saving/execute-insert-update-delete):
  immediate execution, tracker independence, affected-row reporting, and explicit
  transactions provide the comparison contract. Its computed-setter examples
  establish the old-row-value model used by the typed Neri assignment builder.
- SQLite, [UPDATE](https://sqlite.org/lang_update.html),
  [DELETE](https://sqlite.org/lang_delete.html),
  [WITH materialization](https://sqlite.org/lang_with.html),
  [conflict resolution](https://sqlite.org/lang_conflict.html), and
  [affected-row counts](https://sqlite.org/c3ref/changes.html): statement semantics,
  selection fences, partial `FAIL` behavior, and direct affected-row counts.
  Counts exclude auxiliary trigger and foreign-key side effects.
- Microsoft, [logging and diagnostics](https://learn.microsoft.com/en-us/ef/core/logging-events-diagnostics/)
  and [simple logging](https://learn.microsoft.com/en-us/ef/core/logging-events-diagnostics/simple-logging):
  execution observability and the distinction between ordinary diagnostics and
  explicitly enabled sensitive data.

The computed-assignment design rechecked the Codd paper, Microsoft update
contract and SQLite UPDATE specification on 2026-09-23. The paper motivates
relational operations; concrete mutation and failure semantics come from the
provider specification and integration contracts.
