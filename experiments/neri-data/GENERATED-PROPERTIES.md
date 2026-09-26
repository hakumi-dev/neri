# Store-generated scalar properties

Mapped non-key columns can opt into database value generation:

```json
{"member": "label", "column": "label", "default": "new", "valueGeneration": "onAdd"}
```

`onAdd` omits the column from INSERT and reads its value through typed RETURNING.
`onAddOrUpdate` also omits it from UPDATE and refreshes it when another field
changes. Both accept Bool, Int, finite Float and String, including optional
forms. The existing `keyGeneration: "onAdd"` remains the configuration for
generated integer primary keys and temporary tracking identity.

This is an explicit store-owned write policy. On insertion, configured columns
are omitted even when the entity contains nonzero, nonempty or non-null values.
Those values are placeholders until the save succeeds. There is no implicit
sentinel inference or per-save override. After insertion, `onAdd` properties are
ordinary writable fields. Changing a tracked `onAddOrUpdate` property locally
is a validation failure; restore its accepted value before retrying. An entity
with no changed writable fields produces no UPDATE merely to refresh values.

Generated properties can also be [concurrency tokens](CONCURRENCY-TOKENS.md).
Rebase refreshes store-owned `onAddOrUpdate` fields from the supplied database
row while retaining writable local values, making an explicit retry possible.

The database supplies the generation mechanism. A typed `default` in the mapping
can populate the generated schema snapshot. A computed expression, SQL expression
default or trigger needs an independently authored database schema; the generator
does not infer it from `valueGeneration`. SQLite AFTER-trigger changes are not
present in RETURNING and are outside this refresh contract. Generated snapshots
describe scalar columns and constant defaults, not computed expressions.

## Validation and transactions

The tracker describes requested output with ordered `ResultColumns`. SQLite
accepts 1 through 1000 output columns per write and validates names, storage
types, Bool values, Float finiteness, UTF-8 text and nullability. Each tracked
command requesting RETURNING must affect and return exactly one row. The tracker independently checks
returned shape and scalar types, copies validated values and checks generated
key collisions before applying them.

A standalone save validates the complete returned batch before committing,
then applies values to the same tracked instances and accepts their baselines.
Failed statements, invalid output and failed commits leave placeholders and
temporary keys retryable. Copying returned values prevents provider-owned row
mutation during commit from changing the validated result.

Within an explicit transaction, returned values are provisional. Rollback
restores the generated values overwritten by the transaction and the earlier
tracking baseline; other local edits remain available for retry. Rollback
also preserves an `onAdd` field edited after its INSERT when its current value
differs from the last value applied by the store. Graph writes
must apply a principal's generated key before dependent commands. Their owned
savepoint restores generated values and keys if a later graph command fails,
while retaining earlier successful saves in the outer transaction.

`GeneratedValueColumn(ResultColumn, GeneratedValueTiming)` describes non-key
generation for hand-authored trackers. `EntityTracker` accepts `generatedColumns`
and an `applyGenerated(entity, readonly RawRow)` callback. Metadata is copied.
Callbacks must apply only the supplied generated cells and must preserve other
entity fields; generated code implements this contract. As with existing capture
and apply callbacks, arbitrary user callbacks must obey their contract.

## Boundaries

Generated non-key foreign keys cannot be declared as tracked relationship
members: explicit graph writes own their foreign-key assignments. Generated
principal keys continue to propagate to ordinary dependent foreign keys.
Database-generated types outside the mapped scalar set, refresh after AFTER
triggers, configurable sentinels/save behaviors and computed-schema definitions
are unsupported. Set-based operations retain their existing untracked
execution and stale-tracker semantics.

## Verification and references

```sh
scripts/neri.sh run --project tooling/data --unit generator -- experiments/neri-data/generated-properties/mapping.json
scripts/neri.sh run --project experiments/neri-data --unit generated-properties-contract
scripts/neri.sh run --project experiments/neri-data/generated-properties --unit contract
scripts/neri.sh run --project experiments/neri-data/generated-properties --unit contract --release
```

Primary references verified on 2026-09-23:

- Microsoft, [Generated values](https://learn.microsoft.com/en-us/ef/core/modeling/generated-properties):
  distinguishes insert/update generation policies from the database mechanisms
  that implement them. EF's override and sentinel behavior is broader than this
  explicit omission policy.
- SQLite, [RETURNING](https://sqlite.org/lang_returning.html): statement output
  can return generated column values, precedes transaction commit, has no general
  row-order guarantee and excludes later AFTER-trigger modifications. Neri uses
  one row per tracked command and validates it before accepting the save.
- Gray, [The Transaction Concept](https://people.eecs.berkeley.edu/~kubitron/courses/cs262a-S16/handouts/papers/theTransactionConcept.pdf),
  VLDB 1981: the atomic transaction boundary motivates treating returned rows as
  provisional until commit and recovering application state after abort. The
  contracts here do not prove all crash-recovery properties.
