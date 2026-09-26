# Required relationship severing

`unlink_<relation>(dependent)` is generated for optional relationships and for
required relationships configured with `"deleteBehavior": "clientCascade"`.
Handwritten callers use `neri_data::unlinkRelation(context, dependent, slot)`.
The relationship must have an active registered write link.

Optional unlinking clears the foreign key and retains the dependent. Required
client-cascade unlinking marks the dependent and its registered cascade
descendants for removal, then retires the severed link. It keeps the required
scalar FK value intact: it does not assign an invalid null or issue an FK-null
update. The original principal's state is preserved by the operation.
An identifying dependent whose FK is also its primary key can be removed
without changing that key; reassignment and scalar clearing still prohibit
primary-key mutation.

The cascade planner validates all reached entries, restrict dependencies, and
delete ordering before changing states or retiring the link. A closure that
would remove the original principal is rejected. A failed preflight therefore
leaves both entity states and the registered relationship unchanged. Required
restrict relationships continue to reject unlinking.

## Persistence and retry

`saveChanges()` executes the staged deletes. Retired links retain the stored
foreign-key dependency until it is no longer needed. If the caller separately
removes the old principal in the same save, the dependent delete precedes it.
Reassignment followed by required unlinking preserves the original stored
dependency even when the current FK points to a different principal.

Added dependents and their added cascade descendants are cancelled. They remain
detached after outer rollback, while pending deletes of previously stored rows
remain retryable. Entries added during the transaction restore their birth
snapshot and handle identity together, including cancelled entries. A failed
graph save recovers through the existing
[savepoint contract](SAVEPOINTS.md). Rollback preserves the local unlink/removal
intent and does not reactivate the retired link.
When generated keys return to temporary values, unchanged foreign keys that
still refer to their provisional values are repaired as bookkeeping, including
cancelled descendants. This does not reconnect the orphan. Foreign-key edits
that differ from the linked principal before severing, or from the captured
value afterward, remain local edits and are preserved.

This is immediate, explicit orphan staging. Reparent a surviving dependent with
`reassign_<relation>` before unlinking; unlinking a required client-cascade
relationship chooses removal. Deferred conceptual-null state, automatic
collection-change detection, navigation fixup, and configurable orphan timing
are not implemented. Database foreign keys still enforce dependencies outside
the registered graph; missing children can reject the save.

## Verification and references

The runtime cascade contract checks required-policy rejection, descendant
restrictions, protected-principal paths, handle lifetime, and no mutation on
preflight failure. The generated SQLite cascade consumer exercises persisted
orphan removal, retained principals, added cancellation, reparenting, stored
delete ordering, partial failure recovery, and rollback/retry.

```sh
scripts/neri.sh run --project experiments/neri-data --unit cascade-contract
scripts/neri.sh run --project experiments/neri-data --unit cascade-contract --release
scripts/neri.sh run --project experiments/neri-data/cascades --unit contract
scripts/neri.sh run --project experiments/neri-data/cascades --unit contract --release
```

Primary references checked on 2026-09-22:

- Microsoft, [Changing foreign keys and navigations: required relationships](https://learn.microsoft.com/en-us/ef/core/change-tracking/relationship-changes#required-relationships):
  a severed required relationship requires reparenting or dependent removal.
  EF Core also supports deferred orphan processing; Neri currently stages
  removal through the explicit unlink operation.
- Microsoft, [Cascade delete](https://learn.microsoft.com/en-us/ef/core/saving/cascade-delete):
  distinguishes orphan removal, where the principal remains, from removal
  following deletion of the principal. The existing [cascade references](CASCADES.md#verification-and-sources)
  document SQLite enforcement and Markowitz's *VLDB 1991* analysis of cascade
  paths, restrictions, and cycles, used by the shared dependency planner.
