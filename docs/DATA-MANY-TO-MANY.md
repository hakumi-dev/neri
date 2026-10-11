# Many-to-many navigation

`ManyToManyNavigation<L, J, R>` connects two ordinary collection/reference
relationships through an explicitly mapped join entity `J`. Its callbacks read and
replace both skip collections and construct a join. `all(lefts, joins, rights)` and
`load(lefts, joins, rights, rows)` read joins and their referenced targets, deduplicate
object identities and publish both skip collections. Existing inverse members outside
the loaded root selection remain present. All queries must use the same tracked
context and provider. Limits follow ordinary navigation batching.

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
Neither method interprets arbitrary application edits to collection arrays. Ordinary
tracked saves persist the enrolled joins. Implicit join class synthesis and automatic
collection-difference tracking remain separate capabilities.
