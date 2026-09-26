# Generated integer keys

An entity mapping can opt into database-generated integer keys:

```json
{
  "type": "generated_model.Record",
  "set": "records",
  "table": "records",
  "key": "id",
  "keyGeneration": "onAdd",
  "columns": [
    {"member": "id", "column": "id"},
    {"member": "name", "column": "name"}
  ]
}
```

The key must be a required `Int`. Omitting `keyGeneration` retains assigned-key
behavior. For `onAdd`, a new entity starts with key zero; `add` assigns a unique
negative temporary key within the tracker. A nonzero supplied key is rejected.
The INSERT omits the key and requests its database value with `RETURNING`.
SQLite schemas must provide the generation behavior, such as an
`INTEGER PRIMARY KEY`; mapping configuration does not create a schema.

```neri
let record = new Record(0, "example")
let trackingError = db.records.add(record)
let result = db.saveChanges()
```

After a successful save, the same instance contains its returned database key.
All returned rows are validated before keys are applied: output count, required
integer type, and collisions with other returned or actively tracked keys.
Returned negative integers are allowed when they do not collide; temporary
keys are bookkeeping values, not a restriction on the database key domain.

## Transaction contract

A standalone save with generated values opens an internal transaction lease.
It validates the complete batch, commits, then applies keys and accepts tracker
baselines. A failed statement, invalid returned value, or failed commit leaves
the entities retryable with their temporary keys.

Inside an explicit transaction, successful saves apply keys provisionally so
subsequent operations can use them. Outer rollback restores temporary keys and
pending insert state. For a save without relationship edges, returned values
that fail tracker validation after SQLite releases the batch savepoint cause
the entire outer transaction to roll back. This includes collisions with
locally attached keys. Graph saves use an additional context-owned savepoint
to recover execution and returned-value validation failures while preserving
earlier saves; see [graph savepoint recovery](SAVEPOINTS.md). Ordinary statement
failures still use the provider's batch savepoint recovery contract.

`WriteCommand.returningColumns()` describes requested outputs.
`WriteResult.SavedWithValues` is the provider response: its `WriteReturnedRows`
contains one slot per command, with null for commands without returned values.
The context consumes those values and returns `WriteResult.Saved(count)`.
Generated writes require the leased provider path; direct unleased SQLite
batch execution rejects them before starting a transaction.

## Boundaries and verification

This contract describes integer key generation. [Generated scalar properties](GENERATED-PROPERTIES.md)
extend RETURNING to non-key defaults and computed values; [relationship writes](RELATIONSHIPS.md)
propagate generated principal keys to explicitly linked dependents and recover
those assignments on rollback. SQLite RETURNING does not include subsequent AFTER-trigger changes;
schemas that rewrite generated keys in those triggers are outside this contract.

The generated consumer in [generated-values](generated-values/manifest.json)
tests unique-constraint rollback and retry, identity reuse, mixed update/insert
batches, outer rollback after multiple saves, detached collision recovery,
invalid generation schemas, and a deferred foreign-key failure at commit.

```sh
neri run --project tooling/data --unit generator -- \
  "$PWD/experiments/neri-data/generated-values/mapping.json"
neri run --project experiments/neri-data/generated-values --unit contract
neri run --project experiments/neri-data/generated-values --unit contract --release
```

## Verified references

Primary sources checked on 2026-09-22:

- Microsoft, [generated properties](https://learn.microsoft.com/en-us/ef/core/modeling/generated-properties)
  and [temporary values](https://learn.microsoft.com/en-us/ef/core/change-tracking/miscellaneous#temporary-values):
  distinguish temporary tracking identity from database-generated values and
  make generation configuration explicit. Neri writes its temporary key onto
  the entity; EF Core can retain temporary values in its tracker instead.
- SQLite, [RETURNING](https://sqlite.org/lang_returning.html): consume typed
  output from the statement before completion; output is not evidence of commit
  and does not reflect later trigger effects. One row per INSERT avoids relying
  on an unspecified output ordering.
- Gray, [The Transaction Concept](https://people.eecs.berkeley.edu/~kubitron/courses/cs262a-S16/handouts/papers/theTransactionConcept.pdf),
  VLDB 1981: atomicity motivates validating the whole result before commit and
  testing failed commits alongside failed statements. Passing these contracts
  is not a proof of all crash-recovery behavior.
