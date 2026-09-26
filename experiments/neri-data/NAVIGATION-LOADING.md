# Relationship navigation loading

Navigation loading groups database rows into entity collections and assigns the
matching inverse references. It is separate from explicit relationship write
links and from discovering new entities when saving a graph.

## Mapping and use

A relation opts in by declaring both navigation members:

```json
{
  "principal": "navigation_model.Author",
  "dependent": "navigation_model.Book",
  "key": "id",
  "foreignKey": "authorId",
  "collection": "books",
  "reference": "author"
}
```

The principal declares a public `Book[]` field and the dependent a public
`Author?` field. These are ordinary entity fields, separate from mapped scalar
columns. The generator validates their names, access, exact types, and reuse
across relations. Relations without navigation metadata retain their existing
query and explicit-link APIs.

The foreign key may use the principal key's required or optional scalar type.
Null foreign keys are not members of a principal's collection. See
[reference loading](REFERENCE-LOADING.md) for loading from dependent roots,
optional relationships, and string-key equality requirements.

The generated context exposes an include helper:

```neri
let included = db.include_booksFor_Author_by_authorId(db.authors.take(20))
let result = included.all()
```

It returns the principal query's typed result with collections and inverse
references populated. An empty relationship becomes an empty array. The
loader batches up to 50 distinct principal keys per dependent query. Empty
principal results execute no dependent query. Values remain bound parameters.
The relation is bound to an unfiltered dependent root. Its
[`children(query)` selection](FILTERED-NAVIGATION.md) adds filtering, ordering
and per-parent pagination from the same mapped query root.

`included.load(rows)` explicitly reloads relationships for previously obtained
tracked principal rows. Identity belongs to the context. Reloading reuses
tracked entities and replaces collection membership; a removed child's inverse
reference is cleared when it still points at that principal.

## Boundaries

This API loads one collection and its inverse reference. Generated steps compose
it into [multi-level navigation paths](CHAINED-NAVIGATION.md). Automatic context-wide
fixup on arbitrary queries or assignments and composite relationship keys
remain separate capabilities. [Graph registration](GRAPH-REGISTRATION.md) and
[cascades](CASCADES.md) are explicit write operations. Reading navigations does not register
graph write links.

Loading is synchronous and requires provider streaming support for dependent
rows. It stages collections in memory and limits the total children across all
batches to 10,000 by default. `included.maxChildren(maximum)` returns a copy
with a limit from 1 through 10,000; overflow fails rather than silently
returning a partial collection. It does
not make the complete include operation a streaming result.

The child cap is a resource bound, not a throughput guarantee. Duplicate-key
checks and current tracker identity lookup use repeated scans and can take
quadratic time as the number of tracked children grows.

## Consistency and identity

Related data may require several database statements. Without a suitable
transaction, another connection can change the database between them. Loading
does not implicitly promise a single database snapshot. A caller can use an
explicit context transaction when the provider's isolation contract is needed.

Context identity and navigation membership are distinct concerns. Reusing a
tracked entity must preserve its local scalar changes; it must not silently
associate the entity with a principal whose key disagrees with its current
foreign key. Publishing navigation changes follows complete row decoding and
relationship validation.

Both query roots must share their provider and change context. Explicitly
supplied principals and existing navigation participants must be persisted
instances owned by that context. Added, deleted, detached, stale, and foreign
instances, changed keys or foreign keys, and inconsistent inverse references
produce failures. Local changes to other scalar fields survive identity reuse.

A failure before publication preserves collection membership and inverse
references. Tracking has a separate boundary: the root query may already have
tracked principals, and resolving children may have tracked new instances before
a later local relationship conflict. Failure does not roll back tracker
population. The loader also rejects a tracked child whose inverse points to a
selected principal while the principal's current collection omits that child.
It detects this inconsistency during an explicit load; it does not automatically
repair arbitrary navigation assignments.

Generated navigation accessors are ordinary field reads and writes. A custom
`CollectionNavigation<P, D>` is a low-level mapping contract: getters must be
deterministic, side-effect-free reads of the declared mapped primary/foreign
keys and actual navigation fields. Its foreign-key getter must read the field
mapped to `foreignKeyColumn`; setters must perform only the corresponding
navigation assignment, and all callbacks must return normally. Runtime checks
validate observed rows but cannot prove arbitrary callback semantics, especially
when no dependent row matches. Generated descriptors establish that agreement
by construction.

## Verification

`loading-contract` exercises staged publication, identity and ownership checks,
and conflicting local relationship changes. The generated SQLite consumer tests
empty, one, and many children; a 51-parent result split into two dependent
queries; bound string keys containing SQL-looking Unicode text; repeat loads;
malformed rows; and clearing an inverse reference after a database deletion.

Run from the repository root with the local compiler:

```sh
scripts/neri.sh run --project experiments/neri-data --unit loading-contract
scripts/neri.sh run --project experiments/neri-data --unit loading-contract --release
scripts/neri.sh run --project tooling/data --unit generator -- \
  "$PWD/experiments/neri-data/navigation/mapping.json"
scripts/neri.sh run --project experiments/neri-data/navigation --unit contract
scripts/neri.sh run --project experiments/neri-data/navigation --unit contract --release
```

## Verified primary references

These sources were checked on 2026-09-22. They motivate the design; Neri does not
inherit their correctness results or claim complete EF Core behavior.

- James Cheney, Sam Lindley, Philip Wadler,
  [*Query shredding: Efficient relational evaluation of queries over nested multisets*](https://arxiv.org/pdf/1404.7078),
  extended version of the SIGMOD 2014 paper, introduction and sections 4–5.
  It decomposes nested results into flat queries and reconstructs their
  relationships through indexes. Neri's key-batched relation loader is a
  narrower engineering design; it does not implement the paper's general
  shredding translation or its fixed-query-count theorem.
- Microsoft, [eager loading](https://learn.microsoft.com/en-us/ef/core/querying/related-data/eager)
  and [explicit loading](https://learn.microsoft.com/en-us/ef/core/querying/related-data/explicit):
  query-time inclusion and later relationship loading are distinct operations.
- Microsoft, [identity resolution](https://learn.microsoft.com/en-us/ef/core/change-tracking/identity-resolution)
  and [relationship changes](https://learn.microsoft.com/en-us/ef/core/change-tracking/relationship-changes):
  tracked identity, foreign keys, references, and collections must remain
  coherent. EF's context-wide automatic fixup is broader than explicit loading.
- Microsoft, [single versus split queries](https://learn.microsoft.com/en-us/ef/core/querying/single-split-queries):
  splitting queries trades row duplication for multiple statements and introduces
  consistency considerations. Ordered pagination needs deterministic keys.
