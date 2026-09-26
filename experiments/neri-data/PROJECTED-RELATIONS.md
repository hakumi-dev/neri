# Structural projections as relational inputs

`pairShape(left, right)` composes scalar or nested pair shapes into
`ProjectedPair<A, B>`, whose fields are `first` and `second`. Its library-owned
constructor assigns each decoded value to the corresponding field. This fixed
layout lets SQL translation follow those fields through later relational
operators without running a client callback.

```neri
let id = expressionShape(intScalar(), quote do |customer: Customer|: Int
  return customer.id
end)
let label = expressionShape(textScalar(), quote do |customer: Customer|: String
  return customer.name + "!"
end)
let source = project(db.customers, pairShape(id, label)).asRelation()
let selected = source.matching(quote do |row: ProjectedPair<Int, String>|: Bool
  return row.first > 2
end)
```

Shapes retain the leaf codec, physical output column and compiler-resolved
member path. Nested pairs preserve every path segment; nullable scalar leaves
retain their declared optional type. Source entities need not be materialized
to compute the projection.

## Composition and execution

Relations support `matching`, `orderBy`, `thenBy`, `skip` and `take`, as well as
the existing joins and grouped/global aggregates. `projectRelation(source,
shape)` selects from an existing relation. Its `asRelation()` permits another
round of composition when the output has a structural layout.

Filters and windows remain ordered stages: taking source rows before filtering
the projected values restricts the filter to that window. Projection parameters
and parameters of each nested source remain separate bound values. PostgreSQL
placeholder numbering is adjusted when an inner statement follows parameters
in an outer selection; this compilation support is not a PostgreSQL adapter.

Composition builds one SQL statement and defers decoding to terminal execution.
Checked scalar requirements propagate through the complete plan, including
expressions computed by an inner projection. Intermediate relations have no
implicit buffered row cap. Explicit limits remain part of the query.

Ordering set on a `Query` or `Relation` survives conversion to a relation,
filtering, windows and subsequent `projectRelation` calls. Hidden result columns
retain sort keys omitted by the new shape. Query-origin computed sort keys are
selected from the completed source stage, after its original-row distinct and
explicit windows, then used for an explicit outer order. Effective input bounds
remain available after conversion, including a bound in an earlier query stage.

Buffered terminals require an explicit bound between 1 and 1000. Converting a
bounded projection to a relation retains that bound; a new `projectRelation`
terminal accepts its own `take`. Repeated bounds can only narrow the result
and invalid bounds remain failures. Streaming visits decoded results
synchronously and permits an unbounded source.

Relation source columns follow the complete mapped SQL selection. A terminal
decoder may consume fewer fields; that does not remove fields needed by later
grouping, projection or joins. Source entity decoders remain deferred.

## Materialization boundary

`combineShapes(left, right, materialize)` remains available for arbitrary final
DTO construction. Its callback can transform, swap or discard its arguments,
so the selected columns do not establish a relational mapping for the returned
object. Converting that shape to a relation is rejected before execution.
Pair shapes containing an uncertified child are likewise terminal-only.

Only the built-in scalar codecs certify that their decoded value preserves the
SQL scalar. A custom codec may transform a cell even when its declared type and
column kind match, so its shapes remain terminal-only. Custom codecs continue
to work for final projections.

General quoted DTO constructors remain open work. Structural pairs provide
composable result layouts, including [grouped results](GROUPED-RELATIONS.md);
they do not establish translation of arbitrary application constructors.

A standalone scalar shape is still a terminal projection. Wrap scalar leaves
in a pair to obtain the member paths required by a projected relation.

## Verification

```sh
scripts/neri.sh run --project experiments/neri-data/provider-contract --unit projected-relations
scripts/neri.sh run --project experiments/neri-data/provider-contract --unit projected-relations --release
```

The native contract combines nested pairs, optional leaves, captured join
parameters, projected windows and grouping. It also verifies that uncertified
materializers and transforming codecs cannot become relational inputs.

## Verified primary references

Verified on 2026-09-23:

- Microsoft, [client versus server evaluation](https://learn.microsoft.com/en-us/ef/core/querying/client-eval):
  EF Core permits client evaluation in the final projection and rejects
  untranslatable expressions elsewhere. This informs the separation between
  terminal callbacks and SQL-composable layouts; it does not imply API parity.
- Cheney, Lindley and Wadler, [A Practical Theory of Language-Integrated
  Query](https://homepages.inf.ed.ac.uk/slindley/papers/practical-theory-of-linq.pdf),
  ICFP 2013: typed quoted queries and record structure motivate composition
  into a single SQL query. Neri uses explicit plans and decoders and does not
  inherit the paper's normalization theorem.
- SQLite, [SELECT processing](https://www.sqlite.org/lang_select.html), sections
  2.1, 4 and 5: subqueries supply relational inputs, while ordering and limits
  determine each selected window. An inner ordering alone does not promise
  the order of an outer result.
