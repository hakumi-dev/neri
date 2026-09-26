# Navigation graph registration

Generated contexts expose `addGraph_<set>(root)` for registering a connected
object graph before `saveChanges`. The result is `TrackHandleResult<T>` for the
root. Discovery follows mapped collection and reference navigations in both
directions. A scalar foreign-key value alone does not identify an object to
visit.

An untracked reachable object becomes Added. An already tracked instance retains
its state and identity; deleted instances are rejected. Generated integer keys
must be zero for newly added objects. Attach an existing disconnected principal
before enrolling a graph that refers to it. Assigned integer or text keys do not
indicate whether a row already exists in the database.

Repeated references to one object are visited once. Distinct objects claiming
the same tracked key fail registration. Ordinary inverse navigation cycles are
valid; an insert dependency cycle fails preflight. Populated navigations that
claim different principals for the same dependent foreign-key slot also fail.
One-sided navigations register the relationship without filling the missing
inverse navigation. Collection arrays and reference members keep their original
values. A later graph registration may discover newly added descendants of an
already tracked root.

Discovery uses a work queue rather than recursive calls through the object
graph. Registration has a shared bound of 10,000 nodes and 10,000 staged links;
the limit applies across all mapped entity types.

## Failure and save semantics

Registration is an in-memory operation. It enrolls entities and write links,
propagates temporary keys, and checks dependency order. Database writes occur
only through `saveChanges`.

A registration failure restores the state from before the operation, including
temporary keys, foreign keys, tracker identities and relationship registrations.
Unrelated tracked work remains available. This recovery is separate from the
transaction and savepoint journals used when executing database writes.

After successful registration, a database save failure retains the graph's local
intent for retry under the existing [relationship save contract](RELATIONSHIPS.md).
An outer transaction rollback similarly restores the database baseline while
retaining local graph intent. Registration rollback and database rollback have
different purposes and lifetimes.

## Capability boundary

`Query.add` remains the scalar enrollment operation. Graph discovery is requested
through the generated context method. Subsequent navigation edits are not
automatically detected by `saveChanges`; relationship reassignment, unlinking and
cascade operations retain their explicit APIs.

Discovery uses the mapped navigation pair and scalar relationship metadata.
Composite keys, graph-wide disconnected state inference, and context-wide
automatic relationship fixup remain separate capabilities. A registration call
requires exclusive access to its context and entity graph. Handwritten callbacks
must faithfully read and assign the declared fields; unrelated callback side
effects and panics are outside recovery guarantees.

## Verification

The [SQLite consumer](graph-registration/manifest.json) covers generated-key
chains, inverse cycles, shared and attached principals, duplicate-key rollback,
contradictory navigations, dependency cycles, late link conflicts in an open
transaction, and database failure/rollback followed by retry. Its staging
contract also checks a setter that mutates the foreign key incorrectly, then
verifies restoration and a successful retry with the original temporary-key
sequence.

```sh
scripts/neri.sh run --project tooling/data --unit generator -- \
  "$PWD/experiments/neri-data/graph-registration/mapping.json"
scripts/neri.sh run --project experiments/neri-data/graph-registration --unit contract
scripts/neri.sh run --project experiments/neri-data/graph-registration --unit contract --release
```

## References

Verified on 2026-09-23:

- Microsoft, [Explicitly tracking entities](https://learn.microsoft.com/en-us/ef/core/change-tracking/explicit-tracking):
  adding a root can track related entities as Added; generated and assigned keys
  have distinct state-inference implications. This is the comparison contract,
  not a claim that every EF Core tracking policy is implemented.
- Microsoft, [Saving related data](https://learn.microsoft.com/en-us/ef/core/saving/related-data):
  navigation reachability supplies related objects for persistence.
- Microsoft, [Changing foreign keys and navigations](https://learn.microsoft.com/en-us/ef/core/change-tracking/relationship-changes):
  relationship fixup connects navigations and foreign-key values. Neri's graph
  registration is an explicit operation, not continuous context-wide fixup.
- A. B. Kahn, [Topological sorting of large networks](https://doi.org/10.1145/368996.369025),
  *Communications of the ACM* 5(11), 558–562, 1962. The publisher's metadata and
  abstract were verified; the full paper was inaccessible. This is the
  algorithmic reference for dependency ordering. The runtime's repeated scans
  do not claim queue-based linear complexity.
