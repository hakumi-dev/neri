# Filtered collection loading

A generated collection include accepts a typed child query:

```neri
let children = db.books.where(published: true).orderBy(quote do |book: Book|: Int
  return book.rank
end).skip(1).take(2)
let result = db.include_booksFor_Author_by_authorId(db.authors.take(20))
  .children(children).all()
```

The child query uses the same mapped set, tracker, provider and change context
as the relation's original dependent query. Its filter runs on database values;
ordering and pagination select children independently for each parent. A
primary-key tie-break makes each page deterministic. The global
`maxChildren` bound still limits all selected children across every parent and
batch, and overflow fails instead of publishing a truncated result.
Children are streamed into the bounded navigation stage, so a collection load
can exceed the ordinary buffered query limit of 1,000 rows while respecting its
explicit total bound of at most 10,000 children.

Generated `collectionStep_<relation>(maximum: ..., children: ...)` factories
accept the same typed child query for a chained navigation path. Later steps
visit only children selected by the filtered step.

## Query and provider contract

The supported child query has one filter/order/page stage. `where`, supported
typed predicates, primary and secondary ordering, `skip` and `take` compose
within that stage. A frozen earlier stage or `distinct` is rejected before
reading parents; the loader does not reinterpret a global intermediate page as
a per-parent page.

Paged SQL numbers rows with `ROW_NUMBER()` partitioned by the mapped foreign
key, then selects the requested interval. Parent keys and captured values remain
bound parameters. The existing batches of at most 50 distinct parent keys keep
the number of child statements independent of the number of children. Nullable
foreign keys with no matching parent do not enter a collection.
Filtering and ordering without pagination use an ordinary ordered query and
do not require window-function support.

The provider must advertise `WindowFunctions()` for per-parent pagination,
streaming for child reads, and checked scalar expressions when the selected
predicate or ordering requires them. SQLite checks its version and probes
window-function execution when opening the connection. Unsupported capabilities,
invalid child query ownership and unsupported query shapes fail during preflight,
including when the parent query would return no rows.

## Identity, reload and failure

Collection membership and order come from the database query. Reusing an
already tracked child preserves its local scalar changes, so its current
in-memory values may differ from the values that determined SQL filtering or
ordering. Other tracked children are not appended to the filtered collection.

Reload replaces collection membership and clears the inverse reference of
previously loaded children that are excluded. Foreign-key fields and scalar
tracking remain unchanged. Existing local relationship conflicts still fail the
load. Navigation publication occurs only after every batch decodes and validates;
an overflow or later error preserves earlier collection/reference values.
Chained loading uses its existing path journal to reverse an earlier stage if a
later stage fails. Tracker population itself is not rolled back.

Each call explicitly reloads its requested selection. Loaded-state metadata,
lazy loading and automatic context-wide navigation fixup remain separate
capabilities. Multiple statements do not imply a shared database snapshot;
callers choose an explicit transaction when that isolation is needed.

## Verified references

Checked on 2026-09-23:

- Microsoft, [Eager loading: filtered include](https://learn.microsoft.com/en-us/ef/core/querying/related-data/eager):
  the comparison surface includes filtering, ordering and per-collection
  pagination. EF's automatic fixup and loaded-state behavior are broader than
  this explicit Neri loading contract.
- Microsoft, [Single vs. split queries](https://learn.microsoft.com/en-us/ef/core/querying/single-split-queries):
  deterministic ordering and the consistency boundaries of multiple statements.
- SQLite, [Window functions](https://sqlite.org/windowfunctions.html):
  `ROW_NUMBER`, partitioning and window ordering; support began in SQLite 3.25.0.
- James Cheney, Sam Lindley, Philip Wadler,
  [*Query shredding: Efficient relational evaluation of queries over nested multisets*](https://arxiv.org/pdf/1404.7078),
  extended SIGMOD 2014 paper, introduction and sections 4–5. Decomposing nested
  results into flat database queries and reconstructing collections informs the
  loader architecture. Neri uses key batching and per-partition pagination; it
  does not implement the paper's general shredding translation or inherit its
  fixed-query-count theorem.
