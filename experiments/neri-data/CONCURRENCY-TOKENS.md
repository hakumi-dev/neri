# Configurable concurrency tokens

An entity mapping can select the original scalar values that protect tracked
updates and deletes:

```json
"concurrencyTokens": ["version"]
```

Names refer to mapped Neri members, including inherited members. The generator
resolves physical SQL column names. Unknown, primary-key and duplicate members
are rejected. Tokens support the mapped scalar types and their nullable forms.

| Mapping | UPDATE / DELETE predicate |
| --- | --- |
| Property omitted | All original mapped values, retaining the existing default |
| `"concurrencyTokens": []` | Original primary key only |
| Nonempty list | Original primary key plus the selected original values |

The key is always included. Predicate terms follow mapped column order; NULL
uses `IS NULL` and other values use bound parameters. Each tracked command must
affect exactly one row. A missing row or changed selected token produces
`WriteFailure.Concurrency`; constraint violations retain their own category.
An unselected column can change concurrently without rejecting the save. Updates
write only locally changed writable fields, leaving unrelated store edits intact.

Selection does not generate a new token. Applications must change an ordinary
version property when the protected edits warrant it. A store-generated token
can use `valueGeneration: "onAddOrUpdate"` with an independently defined database
generation mechanism. Typed RETURNING refreshes the same instance and accepted
baseline after successful writes. A computed token protects only the inputs
that change its result; it is not automatically a whole-row version.

## Conflict recovery

Failed saves retain local edits. Fetch fresh values using an untracked query or
a separate context, then choose the recovery policy explicitly:

- `rebase(local, fresh)` uses the fresh original baseline and keeps local
  writable fields, including application-managed tokens and `onAdd` properties.
  It refreshes `onAddOrUpdate` properties from the supplied row so the next save
  can retry without presenting stale store-owned values as local edits.
  Pending deletes remain deletes. In an active transaction, rollback restores
  the overwritten generated values and the prior tracking baseline.
- `refreshFrom(local, fresh)` applies the fresh mapped values and cancels the
  pending update or delete.

Rebase validates metadata, rows, key and state before applying generated values.
Entities already saved in an active transaction cannot be rebased. Refreshing
the baseline authorizes a client-wins retry; the application chooses which
writable values to keep and whether to advance an application-managed token.

Hand-authored `EntityTracker` instances use the optional
`concurrencyColumns: String[]?` constructor argument with physical column names.
The tracker copies the array and validates it before writes. Its `null`, empty
and nonempty policies correspond to the mapping policies above.

## Limits and verification

Token comparison follows database comparison semantics. Equal replacement
values, changes later reversed, unselected fields and collation-equivalent
values need not conflict. These per-row checks do not guarantee serializability
of earlier reads or invariants spanning multiple rows. Set-based writes retain
their explicit untracked semantics and do not infer tracked tokens.

```sh
scripts/neri.sh run --project tooling/data --unit generator -- experiments/neri-data/concurrency-tokens/mapping.json
scripts/neri.sh run --project experiments/neri-data --unit concurrency-policy-contract
scripts/neri.sh run --project experiments/neri-data/concurrency-tokens --unit contract
scripts/neri.sh run --project experiments/neri-data/concurrency-tokens --unit contract --release
```

The generated-properties fixture also covers store-generated tokens, failed
stale writes, rebase/retry and rollback. Mapping contracts reject invalid
declarations and check logical-to-physical resolution.

Primary references verified on 2026-09-23:

- Microsoft, [Handling concurrency conflicts](https://learn.microsoft.com/en-us/ef/core/saving/concurrency):
  original-token predicates on updates/deletes, zero-row conflicts, application
  and database-managed tokens, and refreshing originals before retry. Neri's
  omitted-policy default continues to compare all mapped originals; explicit
  empty selection requests key-only writes.
- Kung and Robinson, [On Optimistic Methods for Concurrency Control](https://www.eecs.harvard.edu/~htk/publication/1981-tods-kung-robinson.pdf),
  ACM TODS 1981: transaction validation and retry after conflicting access.
  The paper addresses broader read/write-set validation than these row checks.
