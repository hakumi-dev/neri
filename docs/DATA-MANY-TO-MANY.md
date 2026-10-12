# Many-to-many navigation

`ManyToManyNavigation<L, J, R>` connects two ordinary collection/reference
relationships through an explicitly mapped join entity `J`. Its callbacks read and
replace both skip collections and construct a join. `all(lefts, joins, rights)` and
`load(lefts, joins, rights, rows)` read joins and their referenced targets, deduplicate
object identities and publish both skip collections. Existing inverse members outside
the loaded root selection remain present. All queries must use the same tracked
context and provider. Limits follow ordinary navigation batching.

Generated contexts register `trackCollectionChanges` and materialization fixup.
Independently reading tracked endpoints and active join rows discovers the same
skip relationships through their mapped key tuples and fills both arrays. A
discovered pair is published once, so rereading an endpoint preserves a pending
collection removal. Fixup leaves navigation loading states unchanged and ignores
deleted joins or endpoints. Manual descriptors register this behavior by calling
`trackCollectionChanges(lefts, joins, rights)` once during context construction.

`allAsync` returns an owned `AsyncLoad<L>` with the same traversal. Its operation
shares one deadline and cancellation token across both stages. A navigation journal
reverses completed stage publications if a subsequent read fails, is cancelled or is
disposed before completion. Tracking identities materialized by successful query
stages remain enrolled, as with ordinary navigation loading.

`link(joins, left, right)` constructs the join and publishes collections after
enrollment succeeds. An optional graph-enrollment callback registers both principal
write dependencies atomically; generated contexts supply it so generated keys flow
into join foreign keys. A manually constructed descriptor without that callback
requires already assigned keys and an application-managed write graph.

Identifying join foreign keys may form the primary or alternate key. Registered
relationships can propagate temporary and generated principal keys into an unwritten
`Added` join. Propagation validates the existing baseline, rejects another tracked
identity and restores the foreign key and baseline when graph enrollment or a save
rolls back. Keys on persisted or provisionally written joins remain immutable; direct
application edits to tracked keys are rejected.

Link new endpoint pairs sequentially through the generated helper. General graph
enrollment registers all nodes before propagating relationships, so multiple join
objects with the same initial assigned composite key in one graph are rejected
atomically. It does not synthesize distinct temporary composite join identities.

`unlink(joins, left, right)` requires the complete loaded left join collection. It
validates tracked joins before scheduling their removal and updating both collections.
Ordinary tracked saves persist the enrolled joins. Generated descriptors register
`trackCollectionChanges`, which compares both ordinary skip arrays with tracked
join membership before saving and calls `link` or `unlink` for each changed pair.
Editing either side updates both inverses. Removal requires a complete loaded
left join collection; inconsistent tracked endpoints fail before writes.
The save journal restores navigation publication and tracker enrollment on a
detector failure. Manually constructed descriptors can register the same hook.

A many-to-many mapping may supply an explicit join entity, or omit it and supply
`joinTable` so the generator creates an internal join entity and typed navigation
descriptors. Implicit joins use the same tracking and write graph contracts.
