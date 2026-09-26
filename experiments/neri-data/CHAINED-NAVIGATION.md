# Chained navigation loading

Generated navigation steps compose collection and reference loading across
multiple entity types. The root query retains its filters, ordering and limits;
the terminal returns its root entities with the requested graph populated.

```neri
let query = includePath(
  db.authors.take(20),
  db.collectionStep_booksFor_Author_by_authorId()
).thenInclude(db.collectionStep_pagesFor_Book_by_bookId())

let result = query.all()
```

`NavigationQuery<Root, Current>` carries the root type and the type reached by
the current path. `thenInclude(step)` extends that path from `Current`.
`include(step)` starts another branch at `Root` after the previously requested
steps, and changes `Current` to the new branch's related type. For example:

```neri
let graph = query.include(db.collectionStep_booksFor_Author_by_authorId()).thenInclude(db.referenceStep_booksFor_Publisher_by_publisherId())
```

The generator exposes `collectionStep_<relation>(maximum = 10000)` and
`referenceStep_<relation>()` for relations with collection/reference metadata.
Low-level mappings can use `collectionStep(principals, dependents, descriptor,
maximum)` and `referenceStep(dependents, principals, descriptor)` with the same
`CollectionNavigation` contract as the existing loaders.

Builders are immutable and execute no queries. `all()` executes the root once;
`load(rows)` uses explicitly supplied tracked roots without rerunning that root
query. Related entities are deduplicated by object identity between levels.
Null references contribute no row to the next level. A reference followed by a
collection loads the complete collection for each distinct resolved principal.

## Ownership, limits and failure recovery

Every step must share the root provider and change context, and its source
tracker must match the preceding stage's target tracker (or the root tracker
for a root branch). Filtered queries over the same tracked set are compatible.
The complete chain is validated before root execution. The underlying loaders continue to check
mapped keys, persisted ownership and consistent inverse navigations. Local
scalar edits survive tracked identity reuse. Loading does not create explicit
graph write links or save relationship changes.

Each terminal owns a navigation undo journal. Before assigning a collection or
reference, the loader records its prior value. A later failure restores these
assignments in reverse order, including the original collection array objects.
Successful completion discards the journal. Tracker population is separate:
roots and related instances materialized before failure can remain tracked.

Each level stages and validates its own rows before publishing navigations.
Earlier levels are provisionally assigned while later queries execute; this is
not isolation from application callbacks or concurrent observers. Providers,
navigation callbacks and diagnostic observers must not mutate or reenter the
graph load. Descriptor callbacks must return normally and assign only their
declared navigation field. Failures returned by the loader trigger recovery;
arbitrary callback panics are outside that contract.

Execution remains synchronous and uses the existing batches of at most 50 keys.
An empty related set issues no lookup at the next level. Repeated branches can
repeat database queries; the implementation does not merge paths or promise a
fixed number of statements. Root buffering keeps the ordinary query bound;
explicit root loads and intermediate related sets are limited to 10,000 rows.
Collection steps retain their configurable child cap, and a chain accepts at
most 32 steps. Identity scans and undo storage have bounded but nonconstant
cost; these limits are not a performance guarantee.

Multiple statements do not automatically observe one database snapshot. Use an
explicit context transaction when its isolation behavior is required. Automatic
context-wide fixup, filtered/per-parent limited includes, loaded-state flags,
composite relationship keys and automatic graph discovery are unsupported.

## Verification and references

The native fixture verifies collection/reference paths and sibling branches,
reverse traversal without duplicate identities, local scalar edit preservation,
and rollback after a late malformed row from both empty and populated graphs.
Retained array aliases check storage identity after rollback. Provider counters
prove that the failure reached the last stage, reject incompatible contexts and
trackers before root reads, and cover a 51-root batch boundary. The fixture
passes in debug and release; the existing collection/reference contracts pass.

```sh
scripts/neri.sh run --project tooling/data --unit generator -- experiments/neri-data/chained-navigation/mapping.json
scripts/neri.sh run --project experiments/neri-data/chained-navigation --unit contract
scripts/neri.sh run --project experiments/neri-data/chained-navigation --unit contract --release
scripts/neri.sh run --project experiments/neri-data --unit loading-contract
scripts/neri.sh run --project experiments/neri-data --unit reference-loading-contract
```

Primary references verified on 2026-09-23:

- Microsoft, [Eager loading](https://learn.microsoft.com/en-us/ef/core/querying/related-data/eager):
  multi-level `ThenInclude` paths and multiple branches from the same root
  motivate the typed builder. Neri uses generated step descriptors rather than
  translating navigation lambdas or implementing all EF include behaviors.
- Microsoft, [Single versus split queries](https://learn.microsoft.com/en-us/ef/core/querying/single-split-queries):
  multiple statements avoid some duplicated rows but have consistency and
  buffering tradeoffs. Neri's per-level batching does not imply snapshot isolation.
- Cheney, Lindley and Wadler, [Query shredding: Efficient relational evaluation
  of queries over nested multisets](https://arxiv.org/pdf/1404.7078), SIGMOD 2014,
  extended version: flat query results can reconstruct nested related values.
  This typed loader composes existing key-batched queries; it does not implement
  the paper's general shredding translation or fixed-query-count theorem.
