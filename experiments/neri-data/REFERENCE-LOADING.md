# Reference navigation loading

Reference loading starts from dependent entities and resolves their principal
references. It reuses the paired navigation metadata described in
[navigation loading](NAVIGATION-LOADING.md), including ordinary entity arrays
for inverse collections.

```neri
let books = db.includeReference_booksFor_Author_by_authorId(
  db.books.take(20)
).all()
```

The generated helper returns `ReferencedQuery<Author, Book>` and its `all()`
terminal returns `QueryResult<Book>`. `load(rows)` performs the same operation
for previously tracked dependent rows. It uses the supplied rows without
rerunning the root query's filters or limit. The dependent root can have filters,
ordering, and a limit. The principal query used by the low-level
`includeReference(dependents, principals, relation)` API must be an unfiltered
root from the same provider and change context.

## Optional relationships

Primary keys remain required `Int` or `String`. A foreign key can have that
same type or its optional form, `Int?` or `String?`. The reference field is
`Principal?` in both cases because a required relationship can be unloaded.

A null foreign key represents no relationship. It needs no principal lookup
and leaves a consistent null reference unchanged. A non-null foreign key whose
principal is absent fails explicitly, even when the foreign-key type is
optional. This distinguishes absence of a relationship from a dangling key.
Schema constraints remain a separate responsibility of the database schema.

Collection loading also accepts these optional foreign keys. Its equality
predicates select only dependents whose non-null key matches the principal;
unrelated null-key dependents do not enter the collection.

## Identity and collection membership

The loader deduplicates non-null foreign keys and queries principals in batches
of at most 50 bound values. Empty roots and roots with only null foreign keys
execute no principal query. Several dependents that refer to the same key use
the same tracked principal object.

Reference loading adds selected dependents to the principal's existing inverse
collection without duplicating the same object. Other valid members remain in
the collection. This collection can be partial: resolving one book's author
does not query that author's other books. Use collection loading when complete
membership from the database is required. No loaded-state flag or automatic
context-wide fixup is implied by array contents.

Tracking queries preserve current and original scalar values of an existing
instance. Reference loading therefore follows the tracked foreign key; it is
not an implicit scalar refresh from the database. A pending key or foreign-key
edit, conflicting inverse, or non-persisted participant fails rather than
silently changing relationship intent. Other local scalar edits survive.

All lookups, decoding, identity resolution, and relationship validation precede
publication of reference and inverse-array changes. Failure preserves those
navigations. Root entities and newly resolved principals may already have been
added to the tracker; failure does not undo that tracking. Custom descriptor
callbacks have the same author preconditions as collection loading.

## Execution boundary

Execution is synchronous and stages results in memory. Multiple statements do
not implicitly share a database snapshot; callers can choose an explicit
transaction under their provider's isolation contract. Reading navigations
does not create graph write links or persist relationship changes.

Explicit `load(rows)` accepts at most 10,000 root rows. Principal results are
bounded by the number of distinct requested non-null keys; duplicate,
unexpected, and missing keys fail. Buffered root queries retain the standard
1-through-1,000 limit. Key and tracker scans can take quadratic time; these
bounds do not establish a throughput guarantee.

String identity uses exact Neri string equality. Schemas whose key equality
depends on case-insensitive or custom collations need matching key-comparer
support; this loader does not supply it. SQLite foreign-key comparison can
use the parent column's collation, so database referential integrity alone
does not establish compatibility with Neri's current identity comparison.

Generated steps compose references and collections into
[chained includes](CHAINED-NAVIGATION.md). Composite and alternate relationship keys, automatic fixup
after arbitrary assignments, graph discovery, and cascade behavior remain
separate capabilities. Nullable integer and string foreign keys also support
explicit [graph write links](RELATIONSHIPS.md); loading a reference does not
register such a link automatically.

## Verification

`reference-loading-contract` verifies optional null keys, local scalar
preservation, missing principals, pending relationship edits, and hidden
conflicting inverse collections. The generated SQLite fixture covers optional
integer and Unicode string keys, shared and self-referencing identity, partial
inverse collections, 51 distinct keys across two batches, and late failures
without navigation publication. Optional collection loads verify both key types
and exclude unrelated null-key rows. The existing navigation fixture also loads a
required reference before loading the complete inverse collection.

```sh
scripts/neri.sh run --project experiments/neri-data --unit reference-loading-contract
scripts/neri.sh run --project experiments/neri-data --unit reference-loading-contract --release
scripts/neri.sh run --project tooling/data --unit generator -- \
  "$PWD/experiments/neri-data/reference-navigation/mapping.json"
scripts/neri.sh run --project experiments/neri-data/reference-navigation --unit contract
scripts/neri.sh run --project experiments/neri-data/reference-navigation --unit contract --release
```

## Verified primary references

These sources were checked on 2026-09-22. They inform the contract without
establishing full EF Core equivalence.

- Microsoft, [one-to-many relationships](https://learn.microsoft.com/en-us/ef/core/modeling/relationships/one-to-many):
  nullable foreign keys distinguish optional relationships; requiredness does
  not require a principal to have any dependents.
- Microsoft, [relationship changes](https://learn.microsoft.com/en-us/ef/core/change-tracking/relationship-changes):
  fixup connects already queried or tracked entities without fetching more
  rows. Neri performs the described updates during explicit navigation loading.
- Microsoft, [tracking queries](https://learn.microsoft.com/en-us/ef/core/querying/tracking):
  materialization reuses tracked instances and preserves their current and
  original property values.
- SQLite, [foreign-key comparison rules](https://www.sqlite.org/foreignkeys.html):
  parent-key collation and affinity determine database equality; nullable child
  keys can represent the absence of a relationship.
- Cheney, Lindley, and Wadler,
  [*Query shredding: Efficient relational evaluation of queries over nested multisets*](https://arxiv.org/pdf/1404.7078),
  SIGMOD 2014, extended version: flat queries and key-based reconstruction
  provide the research background for assembling related object results.
  Neri's batched loader does not implement the general shredding translation
  or its fixed-query-count theorem.
