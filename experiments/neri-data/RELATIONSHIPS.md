# Tracked relationship writes

Generated contexts expose explicit links for relationships with `Int` or `String`
primary keys and foreign keys of the same scalar type, including nullable foreign
keys. `addWithHandle` and `attachWithHandle` return a
`TrackHandleResult<T>` containing the tracked entry's opaque handle. A generated
method such as `link_booksFor_Author_by_authorId(author, book)` registers the
dependency and assigns the principal's current key to the dependent's foreign
key. Both handles must belong to the receiving context and remain tracked.

The link preserves the identity of the tracked entry when an inserted entity's
temporary key becomes a database-generated key. A relationship targets one
mapped foreign-key slot. Conflicting principals for the same dependent slot
are rejected before the foreign key changes.

An optional foreign key may start as `null`; registering a link assigns its
principal's non-null key. An unlinked optional foreign key may remain `null`
when saved. Assigning `null` to a linked foreign key does not remove its link:
the next save propagates the registered principal key again. Text keys use
exact Neri string equality; custom database collations require a compatible
key comparer, which is outside the current contract.

Handwritten mappings can use `ScalarRelationshipLink<P, D>` with `Cell` key
callbacks, an `Int` or `Text` result kind, and foreign-key nullability. Scalar
kind and nullability are validated before assignment. Callbacks must faithfully
read or assign the mapped field and be deterministic; arbitrary callback side
effects are outside rollback guarantees. The required-`Int`
`RelationshipLink<P, D>` API remains available and uses the same engine.

The optional `ClientSetNull` policy retains linked dependents when the explicit
context cascade operation removes their principal. It clears their mapped FK
and retires the link, preserving the old stored dependency for save ordering.
See [client nullification](CLIENT-NULL.md) for mixed graph and rollback behavior.

## Changing an explicit relationship

`reassign_<relation>(newPrincipal, dependent)` replaces a registered link for
that dependent's mapped foreign-key slot. It assigns the new principal key
directly, including for required foreign keys. `unlink_<relation>(dependent)`
assigns `null` for optional foreign keys. For a required relationship configured
with `clientCascade`, it stages [orphan deletion](ORPHANS.md) and preserves the
principal. Both operations
validate tracked ownership and handle lifetime before changing the relationship.
Plain `link_<relation>` continues to reject a conflicting principal; replacing
one is an explicit operation.

Handwritten callers use `ChangeContext.reassignRelation(newLink.edge())` and
the module function `neri_data::unlinkRelation(context, dependent, slot)`. A
fresh link descriptor is required when reconnecting a previously retired link.

The foreign-key slot must identify a mapped column of the declared scalar type
and nullability. A foreign key that is also the dependent's primary key cannot
be reassigned or cleared, because tracked primary keys are immutable. Required
orphan removal can delete an identifying dependent without changing its key. These
checks precede mutation, including for handwritten link descriptors.

These operations require an explicitly registered relationship. When starting
from database-loaded entities, register the existing principal/dependent link
before changing it. Loading navigation properties alone does not register a
write link. An unlink removes key propagation; a later `link_<relation>` may
connect that slot again. Navigation arrays and object references are independent
of these scalar write operations.

Reassignment preserves the old foreign-key dependency while the database may
still reference the old principal. When saving a replacement and deleting the
old principal together, the required order is new-principal insert, dependent
foreign-key update, then old-principal delete. Unlinking similarly writes the
null foreign key before deleting the old principal. Historical dependencies
never propagate the old key back into the dependent.

## Saving and rollback

`saveChanges` plans linked entries before issuing SQL. Inserts run after their
principals, and explicitly requested deletes run before their principals.
Generated principal keys reach dependent inserts inside the same provider
transaction. A dependency cycle fails validation before a transaction opens.
Entity declaration order does not determine dependency order.

Every graph save uses one transaction lease. Each write is validated before its
tracked baseline is accepted provisionally. A failure after execution starts
rolls back that save, including successful earlier actions within it. With an
outer transaction and a savepoint-capable provider, earlier successful saves
remain intact and the failed save can be retried. Native full-transaction abort
restores the outer journal; providers without savepoints also retain whole-lease
rollback. Preflight failures leave an existing transaction active. See
[graph savepoint recovery](SAVEPOINTS.md) for ownership and cleanup semantics.

Rollback restores the tracker journal and then assigns each linked foreign key
from its restored principal key. Temporary generated keys and pending insert or
delete intent become retryable. Local edits and explicit link intent remain in
memory. Registering a link inside an outer transaction therefore does not undo
the link itself when that transaction rolls back. Reassignment and unlink
likewise retain the current local intent. Dependencies needed to retry against
the restored database state survive rollback, including changes across multiple
saves in an outer transaction.

## Capability boundary

Links coordinate tracked scalar foreign keys. Generated
[`addGraph_<set>` operations](GRAPH-REGISTRATION.md) discover objects through
mapped navigation properties and register write links atomically. Scalar
`Query.add` enrolls one entity. Explicit
[navigation loading](NAVIGATION-LOADING.md) populates collections and inverse
references independently. Automatic context-wide fixup and composite
keys remain separate capabilities. Opt-in
[client cascade deletion](CASCADES.md) traverses registered links through the
generated `removeCascade_<set>(handle)` operation. The default restrict policy
requires explicit dependent removal; scalar entity-set `remove` stages one row.

The implementation performs repeated dependency scans; it does not claim the
linear-time complexity of a queue-based topological sorting implementation.

## Verification and references

The [generated SQLite consumer](relationships/manifest.json) declares dependents
before principals and uses immediate foreign-key enforcement. Its contracts
exercise generated-key propagation, a dependent constraint failure followed by
retry, multiple saves followed by outer rollback, reverse delete order, and
rejection of foreign-context handles. Runtime contracts cover cyclic links
without beginning a database transaction.

The [scalar relationship consumer](scalar-relationships/manifest.json) exercises
required and nullable text keys, nullable integer foreign keys with generated
principals, parameter-bound Unicode text, unlinked nulls, and rollback/retry.
The `scalar-relationship-contract` runtime unit checks invalid scalar kinds,
nullability, stale handles, and retained explicit-link intent.

Relationship-change checks declare principals before dependents to force
update-before-delete ordering under immediate SQLite foreign keys. They cover
pending changes across empty commits, specific dependent CHECK failures,
generated-key rollback/retry, and multiple saves followed by outer rollback.
Runtime checks cover reassignment round trips, unlink/relink, identifying-key
rejection, and single-row self-reference deletion.

```sh
scripts/bootstrap.sh
scripts/neri.sh run --project tooling/data --unit generator -- \
  "$PWD/experiments/neri-data/relationships/mapping.json"
scripts/neri.sh run --project experiments/neri-data/relationships --unit contract
scripts/neri.sh run --project experiments/neri-data/relationships --unit contract --release
scripts/neri.sh run --project experiments/neri-data --unit scalar-relationship-contract
scripts/neri.sh run --project experiments/neri-data/scalar-relationships --unit contract
scripts/neri.sh run --project experiments/neri-data/scalar-relationships --unit contract --release
```

- Microsoft, [Saving related data](https://learn.microsoft.com/en-us/ef/core/saving/related-data):
  EF Core discovers new entities through navigations and saves related changes.
  This supplies the comparison contract; explicit Neri links implement a subset.
- Microsoft, [Relationship changes](https://learn.microsoft.com/en-us/ef/core/change-tracking/relationship-changes):
  the relationship between principal keys, foreign keys, navigation fixup,
  reassignment, and optional relationship severing. Checked on 2026-09-22.
- EF Core, [CommandBatchPreparer](https://github.com/dotnet/efcore/blob/b61f0f82ff3837a9f5b3f40c979441642e97002c/src/EFCore.Relational/Update/Internal/CommandBatchPreparer.cs):
  command dependencies include current and original foreign-key values, so a
  foreign-key update precedes deletion of its previous principal. This internal
  implementation is a design reference, not a public API dependency.
- Microsoft, [Foreign and principal keys](https://learn.microsoft.com/en-us/ef/core/modeling/relationships/foreign-and-principal-keys):
  scalar type compatibility and optional versus required foreign keys. This
  reference and the saving/SQLite references were checked on 2026-09-22.
- A. B. Kahn, [Topological sorting of large networks](https://doi.org/10.1145/368996.369025),
  *Communications of the ACM* 5(11), 558–562, 1962. Publication metadata and
  abstract were verified on 2026-09-22; the full ACM paper was inaccessible. This is the
  algorithmic reference for dependency ordering, not evidence of an exact
  implementation or complexity match.
- SQLite, [Foreign key support](https://sqlite.org/foreignkeys.html) and
  [transactions](https://sqlite.org/lang_transaction.html): enforcement and
  all-or-nothing transaction behavior used by the integration fixtures.
