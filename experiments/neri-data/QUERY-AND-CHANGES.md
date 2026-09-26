# Query composition and tracked changes

Neri Data combines typed query construction with a generated context and
provider-independent change tracking. SQLite supplies native reads and atomic
write batches. A context borrows its provider and is used sequentially.

## Query contract

`where` adds typed equality filters. `matching` inspects a quoted predicate.
`orderBy`, `orderByString`, `orderByBool`, and `orderByFloat` select mapped sort fields. These
operations construct a query; a terminal executes it.

Quoted predicates can compare [two direct mapped fields](COMPARISONS.md) with
the same scalar type. Nullable equality and its negation preserve Boolean null
semantics. Required integer and Float fields also support ordered comparisons.
Mapped `Float` and `Float?` values use the [finite binary64 contract](FLOATS.md).

Queries preserve operation order with nested SQL when needed. For example,
`orderBy(id).take(2).where(active: false)` filters those first two rows.
`skip` applies an offset at its position in the pipeline; subsequent filters
and ordering operate on the remaining rows. Repeated adjacent valid `take`
calls retain the smaller limit. An invalid limit stays a failure through later
operators. Limits are 1 through `queryRowLimit()`; `take(0)` is an error.

`thenBy`, `thenByString`, `thenByBool`, and `thenByFloat` add secondary mapped sort fields and
require a preceding primary ordering. Outer SQL stages re-emit the effective
ordering. `distinct()` removes duplicate complete mapped rows before later
projections; unmapped physical columns do not participate in that comparison.

| Terminal | Result | Limit and tracking |
| --- | --- | --- |
| `all()` | `QueryResult<T>` | Requires a bound in the pipeline; materializes bounded rows and tracks generated entities. |
| `stream(visitor, options)` | `StreamResult` | Visits untracked decoded rows synchronously; unbounded input is allowed. Supports explicit stop, cooperative cancellation, and provider deadlines. |
| `first()` | `QueryValue<T?>` | Reads at most one entity, tracks it, and returns null for an empty result. This has first-or-default semantics. |
| `firstOrDefault()` | `QueryValue<T?>` | Explicitly named alias for `first`. |
| `single()` | `QueryValue<T>` | Requires exactly one row; reports a cardinality error on zero or multiple rows. |
| `singleOrDefault()` | `QueryValue<T?>` | Returns null on zero rows and a cardinality error on multiple rows. |
| `any()` | `QueryValue<Bool>` | Tests database row existence without constructing entities; no explicit `take` is required. |
| `count()` | `QueryValue<Int>` | Counts the composed input in SQL, including limits, offsets, and distinct; unbounded input may scan all matching rows. |

Invalid queries fail before execution. A query without explicit ordering has
no guaranteed first row. Repeating `orderBy` replaces the preceding ordering
at its position in the pipeline. Typed [inner/left joins](JOINS-AND-PROJECTIONS.md)
compose two mapped inputs through separate query wrappers. These wrappers support
[side predicates, ordering, pagination, and projected shapes](JOIN-COMPOSITION.md).
Typed NULL decoding and the existing
nullable equality compensation remain in effect.

Cardinality terminals probe at most two rows and respect earlier limits.
Ambiguous results do not populate the tracker. The reported multiple-row count
is the probe size, not a full database count.

An explicit terminal provider argument performs an untracked read, including
when it happens to refer to the context's own provider. This keeps results from
another connection out of the context's tracker without relying on managed
reference equality. Parameterless terminals use the bound provider.

## Object projections

Nested [projection shapes](JOINS-AND-PROJECTIONS.md) compose scalar selections
into DTOs without a fixed field count. `select` projects one quoted scalar
field. `select2` selects two quoted mapped
fields and materializes a typed result object from their strictly decoded
values:

```neri
let byId = quote do |customer: Customer|: Int
  return customer.id
end
let byName = quote do |customer: Customer|: String
  return customer.name
end
let summaries = select2(db.customers.take(20), intScalar(), byId,
  textScalar(), byName, makeSummary).all()
```

Here `makeSummary` has type `fn(Int, String): CustomerSummary`. The two field
selections execute in SQL. The factory constructs a result after decoding; it
is not a translated predicate or a fallback for unsupported query operations.
Separate output aliases allow selecting the same field twice. Projected objects
and aggregates are untracked. Nested shapes support arbitrary projection arity;
arbitrary quoted object construction and filtering a projected object remain
outside this contract. Scalar, two-field, and nested shape projections support
[synchronous streaming](STREAMING.md) through `stream` or an explicit provider
override with `streamWith`.

## Tracked entities and saving

Generated query roots expose `add(entity)`, `attach(entity)`, and
`remove(entity)`, returning a nullable `QueryFailure` (null means success).
Mutations on a filtered, ordered, limited, or failed query are rejected.

The context retains one entity per mapped key. Repeated tracked reads return
that instance without overwriting local changes. Explicit add/attach rejects
an already tracked key, including repeated calls with the same instance; Neri
does not provide managed reference comparison for this API. Removal identifies
an already tracked entity by its key. Adding and then removing an entity before
saving cancels its insertion.

`attach` records the supplied scalar values as the baseline for an existing
entity. `saveChanges()` compares current mapped values to their baselines,
emits inserts, updates only changed columns, and emits deletes. Assigned keys
are required and immutable while tracked. The context accepts new baselines
after a successful save; an explicit outer transaction keeps those baselines
provisional until commit. A failed save retains pending changes for correction
and retry.

```neri
match db.customers.where(id: 1).first()
  case QueryValue.Value(customer)
    if customer != null
      customer.name = "Updated name"
      match db.saveChanges()
        case WriteResult.Saved(count)
          console::println(count as String)
        case WriteResult.SavedWithValues(count, outputs)
          console::println(count as String)
        case WriteResult.Failure(error)
          console::println(writeFailureMessage(error))
      end
    end
  case QueryValue.Failure(error)
    console::println("Customer query failed")
end
```

SQLite writes require `SQLiteProvider.openWritable(path)` against an existing
database. `open(path)` retains read-only behavior. Writable connections enable
and verify foreign-key enforcement. Each nonempty batch uses `BEGIN IMMEDIATE`
and one commit. Any statement, affected-row check, or commit failure rolls back
the batch. An uncertain rollback makes the provider reject further operations;
the caller must close it.

Updates and deletes compare the original primary key and configured concurrency
columns and require exactly one affected row. Omitting `concurrencyTokens`
compares all mapped originals; an empty list compares only the key. See
[configurable concurrency tokens](CONCURRENCY-TOKENS.md). Changes later reversed,
collation-equivalent changes and changes to unselected columns need not conflict. Reads before
`saveChanges()` are not held inside its transaction, so the complete context
lifetime does not promise serializable execution.

Unlinked batches execute in mapped-set declaration order, then tracking order
within each set. Explicit [tracked relationships](RELATIONSHIPS.md) coordinate
dependent writes and opt-in client cascades. [Generated integer keys](GENERATED-VALUES.md)
and [generated scalar properties](GENERATED-PROPERTIES.md) have opt-in mapping contracts.
SQLite caps a batch at 1000 commands. Applications must sequence saves
appropriately for their schema.

## Inspecting changes and recovering conflicts

`WriteResult.Failure` carries a `WriteFailure`: `Validation`, `Concurrency`,
`Constraint`, `Provider`, `Unsupported`, or `Transaction`. Applications can
match the category; `writeFailureMessage` supplies diagnostic text. SQLite
constraint diagnostics retain its extended error code. A stale update is a
concurrency error, while an insert violating a foreign key or unique index is
a constraint error.

Query roots expose `stateOf(entity)`, `original(entity)`, and `current(entity)`.
`Modified` is derived from current mapped values; original snapshots are copied
so inspection cannot mutate the stored baseline. `detach(entity)` stops tracking
that key and the generated context's `clear()` detaches all mapped sets.

After fetching fresh database values with an explicit, untracked provider
argument, `rebase(local, fresh)` replaces the original baseline and preserves
local writable fields. Store-owned `onAddOrUpdate` fields are refreshed from the
supplied row; rollback restores them when rebasing in an active transaction.
The next save retries the local values against the fresh baseline.
This is an explicit client-wins decision. Both entities must have the tracked
key, and added or detached entries cannot be rebased. Detaching and requerying
supports replacing a local instance with current database values. Tracking
inspection and recovery identify entries by mapped key.

`refreshFrom(local, fresh)` applies supplied database values to the existing
tracked instance and cancels its pending update or delete. Generated callbacks
assign the mapped fields only after schema and key validation succeeds. This
store-wins operation does not fetch rows itself; callers supply an untracked
read. It rejects entries already saved within the active transaction, whose
provisional writes cannot be undone by changing local fields.

## Explicit transactions

Generated contexts expose `beginTransaction()`, `commitTransaction()`, and
`rollbackTransaction()`, each returning `WriteResult` with zero on success.
Begin checkpoints the trackers; each successful save inside the transaction
advances provisional baselines. Commit makes those baselines final. Commit
does not implicitly save unsaved changes.

SQLite uses `BEGIN IMMEDIATE` for the outer transaction and a savepoint for
each nonempty save. A failed save rolls back its savepoint so earlier successful
saves remain pending in the transaction. SQLite's automatic whole-transaction
rollback instead restores the context's journal. Releasing a savepoint is not
a durable commit.

Rollback restores database baselines while preserving local field edits and
pending insert/delete intent for retry. Explicitly detached entries stay
detached. Removing an entity that was added locally cancels that addition even
when an outer transaction has already inserted it provisionally. Outer rollback
keeps the cancelled entry detached, including additions staged before the
transaction began; a later save does not reinsert it. Removing a row that
existed at transaction start preserves its pending deletion instead.
Entities first read during the transaction are detached on rollback,
since their observed baseline may depend on reverted writes. Reusing a deleted
key inside the same open transaction is rejected. Closing SQLite rolls back an
active transaction; the context reconciles that rollback before its next save.

A provider-issued lease rejects writes or transaction control from another
context. Use one context per provider during an explicit transaction: reads on
the same connection can observe its uncommitted writes. Contexts are sequential,
and cross-context transaction enlistment is not implemented. Providers expose
owned savepoints; generated tracked contexts manage them internally rather than
exposing manual nested checkpoints. See [graph savepoint recovery](SAVEPOINTS.md).
An uncertain transaction outcome prevents further writes.

## Verified references

These primary sources were checked on 2026-09-22. They motivate the contracts
and test cases; they do not prove this implementation correct.

| Source | Applied contract and verification |
| --- | --- |
| Meijer, Beckman, Bierman, [LINQ](https://gavinbierman.github.io/assets/pdf/sigmod2006.pdf), SIGMOD 2006; Cheney, Lindley, Wadler, [A Practical Theory of Language-Integrated Query](https://homepages.inf.ed.ac.uk/jcheney/publications/cheney13icfp.pdf), ICFP 2013 | Composable typed queries and inspectable expressions motivate an explicit supported translation vocabulary. Nested SQL preserves operator order; tests cover filters after limits, offset boundaries, and server-selected DTO fields. Neri does not implement the full normalization calculus. |
| Microsoft, [pagination](https://learn.microsoft.com/en-us/ef/core/querying/pagination); SQLite, [SELECT processing](https://sqlite.org/lang_select.html) | Ordering is explicit at each stage. Tests verify secondary ordering, offset before a later filter, and distinct over mapped columns rather than the physical table. Offset does not introduce an implicit inner materialization limit. |
| Gray, [The Transaction Concept: Virtues and Limitations](https://people.eecs.berkeley.edu/~kubitron/courses/cs262a-S16/handouts/papers/theTransactionConcept.pdf), Tandem TR 81.3 / VLDB 1981, abstract and general model | Transaction effects must commit together or be undone. Integration forces a later statement to fail, checks that an earlier insert disappeared, then repairs and retries the pending changes. |
| Berenson et al., [A Critique of ANSI SQL Isolation Levels](https://www.microsoft.com/en-us/research/wp-content/uploads/2016/02/tr-95-51.pdf), SIGMOD 1995, §4.1 | Lost updates motivate a stale-context test. The implementation uses original-value predicates; atomic saving alone does not imply serializability of earlier reads. |
| Microsoft, [snapshot change detection](https://learn.microsoft.com/en-us/ef/core/change-tracking/change-detection), [identity resolution](https://learn.microsoft.com/en-us/ef/core/change-tracking/identity-resolution), and [optimistic concurrency](https://learn.microsoft.com/en-us/ef/core/saving/concurrency) | Track scalar baselines, retain one instance per key, and check configured originals on mutation. Neri preserves all mapped originals as the omitted-policy default. Tests cover identity reuse, unchanged saves, key mutation, selected tokens, and conflicting contexts. |
| Microsoft, [SaveChanges transactions](https://learn.microsoft.com/en-us/ef/core/saving/transactions); Fowler, [Unit of Work](https://martinfowler.com/eaaCatalog/unitOfWork.html) | One context gathers changes across entity sets and submits one atomic batch. In-memory baselines advance only after success. |
| SQLite, [transactions](https://www.sqlite.org/lang_transaction.html), [affected rows](https://www.sqlite.org/c3ref/changes.html), [autocommit state](https://www.sqlite.org/c3ref/get_autocommit.html), and [foreign keys](https://www.sqlite.org/foreignkeys.html) | Use an explicit write transaction, check each mutation's affected rows, account for automatic rollback and commit failures, and enable foreign keys per connection. |
| SQLite, [savepoints](https://sqlite.org/lang_savepoint.html) and [extended result codes](https://sqlite.org/rescode.html) | Savepoint release remains provisional until outer commit. Integration tests force a failed savepoint, outer rollback, and trigger-driven automatic rollback; callers receive distinct constraint and concurrency categories. |

## Focused verification

From the repository root:

```sh
neri run --project experiments/neri-data --unit runtime-contract
neri run --project experiments/neri-data --unit persistence-contract
neri run --project experiments/neri-data/providers/sqlite --unit contract
neri run --project experiments/neri-data/provider-contract --unit persistence
neri run --project experiments/neri-data/provider-contract --unit persistence --release
neri run --project . --unit data-generation-contracts -- "$PWD"
```

The persistence integration uses isolated temporary files. It verifies query
terminals, DTO projections, insert/update/delete, cancelled additions,
snapshot acceptance, key validation, atomic rollback and retry, stale writes,
read-only rejection, and persistence after reopening the database. Native
SQLite verification is currently on macOS.
Explicit-transaction checks cover multiple saves, insert and delete retries,
savepoint recovery, ownership rejection, automatic rollback, and close cleanup.
