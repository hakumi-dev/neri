# Client foreign-key nullification

Configure an optional relationship with `"deleteBehavior": "clientSetNull"` to
retain its explicitly linked dependents when removing the principal through
`removeCascade_<set>(handle)`. The corresponding handwritten policy is
`RelationshipDeletePolicy.ClientSetNull()`. The FK must be nullable and must not
be the dependent's primary key. Required or identifying mappings are rejected.

The operation clears surviving dependent FKs and retires those links. Persisted
dependents remain tracked with pending scalar updates; added dependents remain
added and can be inserted with null FKs. The principal is removed under the
existing [cascade contract](CASCADES.md). The ordinary entity-set `remove`
operation continues to stage one entity.

## Mixed graphs and ordering

The planner computes the complete client-cascade deletion closure before
choosing nullifications. If another path selects a dependent for deletion, it
is deleted without a redundant FK-null update. Surviving restrict dependents
still reject the operation. Ownership, handle lifetime, FK metadata, and graph
order are checked before applying the plan.

Retired links retain their stored FK dependency. `saveChanges()` therefore
updates surviving dependents before deleting their old principal, including
when the database checks `RESTRICT` immediately. Multiple cleared slots on one
dependent are saved as one entity update.

Nullification is local relationship intent. Save failures preserve it for retry;
outer rollback restores tracked baselines without reconnecting retired links.
The [savepoint contract](SAVEPOINTS.md) defines recovery after partial writes.
Generated mappings provide direct field callbacks. Handwritten callbacks must
obey the deterministic field-access contract in [RELATIONSHIPS.md](RELATIONSHIPS.md).
Validation failures leave the plan unapplied. If a handwritten setter violates
that contract during application, its error can follow earlier local changes;
the preflight guarantee does not cover arbitrary callback effects.

## Scope

This policy operates on registered write links. Navigation loading does not
establish completeness or register those links automatically. An unknown
database dependent can still reject the principal delete through its FK, and
the save must recover normally. The mapping does not add database `ON DELETE
SET NULL` actions or change the schema.

The default policy remains explicit restriction. Neri does not adopt EF Core's
default optional-relationship policy or its automatic navigation fixup.
Optional explicit unlinking continues to clear the FK independently of the
configured deletion policy.

Deletion cycles retain the cascade planner's current rejection rule. The
planner does not add an intermediate null update to an entity already selected
for deletion to break a cycle.

## Verification and references

```sh
scripts/neri.sh run --project experiments/neri-data --unit client-null-contract
scripts/neri.sh run --project experiments/neri-data --unit client-null-contract --release
scripts/neri.sh run --project experiments/neri-data/client-null --unit contract
scripts/neri.sh run --project experiments/neri-data/client-null --unit contract --release
```

Primary references checked on 2026-09-22:

- Microsoft, [Cascade delete: optional relationships](https://learn.microsoft.com/en-us/ef/core/saving/cascade-delete#optional-relationship-with-dependentschildren-loaded):
  distinguishes retaining dependents by clearing nullable FKs from deleting
  them. `ClientSetNull` applies in the client and does not configure a database
  set-null action; unloaded dependents can still cause a database error.
- SQLite, [Foreign key actions](https://www.sqlite.org/foreignkeys.html#fk_actions):
  `RESTRICT` checks when a referenced key changes, so client-side FK updates
  must precede principal deletion. `SET NULL` is a separate database action.
- Victor M. Markowitz,
  [Safe Referential Integrity Structures in Relational Databases](https://www.vldb.org/conf/1991/P123.PDF),
  *VLDB 1991*, pp. 123–132: formalizes restrictions, cascades, and nullification
  in referential structures. This motivates examining mixed graph paths before
  mutation; Neri's concrete graph planner does not implement the paper's
  schema-level safeness conditions.
