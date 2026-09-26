# Client cascade deletion

Relationship mappings can opt into client cascade deletion with
`"deleteBehavior": "clientCascade"`. Optional relationships can instead use
[`"clientSetNull"`](CLIENT-NULL.md) to retain dependents and clear their FKs.
The default is `"restrict"`. These policies control explicitly registered
tracked links; they do not change database
foreign-key DDL or discover additional rows through navigation properties.

Generated contexts expose `removeCascade_<set>(handle)`. The operation computes
the transitive set of entries reached through active client-cascade links,
validates the plan, and marks its entries for removal. Call `saveChanges()` to
execute the resulting writes. Handwritten consumers use
`neri_data::removeCascade(context, handle)` and configure
`RelationshipDeletePolicy.ClientCascade()` on their link descriptors.

## Planning and state

Planning uses tracked entry identity, including while generated keys are still
temporary. Multiple paths to the same dependent produce one removal. Restrict
links do not expand the removal set: a surviving dependent blocks deletion,
while a dependent already included through another cascade path can be removed.
The complete cascade set is established before checking these restrictions.
Client-set-null links clear the optional FKs of surviving dependents after
preflight; a dependent selected for deletion through another path is removed
without an additional null update.

The preflight validates ownership, handle lifetime, mapped keys, and dependency
ordering before changing tracked states. Persisted entries become deleted;
unsaved added entries become detached without being inserted. Stored deletes
run dependent-first under the existing graph transaction contract. A single-row
self-reference does not require a separate delete; a multi-entry dependency
cycle that has no valid deletion order is rejected.

Reassignment and unlinking retire the previous link. Retired links retain stored
foreign-key ordering where needed, but do not expand the cascade set. Thus a
dependent moved to another principal is not selected for deletion merely because
its previous principal is removed. Plain field edits do not replace registered
relationship intent; use the explicit relationship operations.

The operation stages local deletion intent. Failed saves remain retryable under
the [graph savepoint contract](SAVEPOINTS.md); outer rollback reconciles stored
baselines while preserving pending local removals. A graph of newly added
entries can be cancelled without issuing SQL inserts. This cancellation also
survives rollback when the entries were added before the outer transaction or
were already inserted provisionally within it.

## Database and loading boundary

SQLite fixtures use enforced foreign keys without database cascade actions, so
successful deletion proves the client emitted the required dependent writes.
An unregistered database dependent is outside the known graph and may cause the
database to reject the principal delete. That save is rolled back; the client
does not silently claim to have removed the missing dependent.

Navigation loading and reference loading can expose partial inverse collections.
Their arrays are not evidence that every dependent is known, and loading alone
does not register write links. Register the intended links before applying a
client cascade. [Required orphan deletion](ORPHANS.md) uses explicit unlinking
with this policy. Navigation fixup and collection-change detection are separate
capabilities; the policy does not infer changes from collection edits.

The existing entity-set `remove` operation still stages one entity. The explicit
context cascade operation is the entry point for this policy. No implicit
required-relationship cascade convention is applied.

## Verification and sources

Runtime contracts exercise transitive closure, shared dependents, restrictions,
atomic preflight rejection, handle lifetime, temporary keys, policy mismatches,
and active/retired dependency cycles. They also cover cancellation of additions
staged before and during an outer transaction, including provisional inserts.
The generated SQLite consumer exercises actual FK enforcement, delete ordering,
save failure after a partial delete and retry, outer rollback, self-reference,
Unicode string keys, and reassignment/unlink boundaries.

```sh
scripts/neri.sh run --project experiments/neri-data --unit cascade-contract
scripts/neri.sh run --project tooling/data --unit generator -- \
  "$PWD/experiments/neri-data/cascades/mapping.json"
scripts/neri.sh run --project experiments/neri-data/cascades --unit contract
scripts/neri.sh run --project experiments/neri-data/cascades --unit contract --release
```

Primary references checked on 2026-09-22:

- Microsoft, [Cascade delete](https://learn.microsoft.com/en-us/ef/core/saving/cascade-delete):
  distinguishes client processing of tracked dependents from database cascades;
  a missing dependent can make a client-cascade deletion fail its database FK.
  Neri's explicit-link scope is narrower than EF Core's relationship tracking.
- SQLite, [Foreign key actions](https://www.sqlite.org/foreignkeys.html#fk_actions):
  `RESTRICT` checks immediately and `CASCADE` is a database action. Client policy
  therefore does not replace schema enforcement or declare a database action.
- Victor M. Markowitz,
  [Safe Referential Integrity Structures in Relational Databases](https://www.vldb.org/conf/1991/P123.PDF),
  *VLDB 1991*, pp. 123–132, especially section 5's comparison of cascade paths,
  restrictions, and cycles. The verified discussion motivates validating the
  whole dependency structure rather than one edge in isolation. Neri validates
  a concrete tracked graph; it does not implement the paper's schema-level
  safeness conditions or claim their guarantees.
