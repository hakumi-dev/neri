# Composing joined queries

Inner and left joins retain their source queries and support further filtering,
ordering, offsets, limits, and server-selected result shapes. Operations execute
in their declared order. Filtering a joined query after `take(2)` filters those
two rows; filtering before `take(2)` selects up to two matching rows.

## Side predicates and presence

`matchingLeft(quote fn(L): Bool)` filters on the left entity's mapped fields.
`matchingRight(quote fn(R): Bool)` filters on the right entity's mapped fields.
They reuse the supported scalar quotation vocabulary and bound parameters.
Predicates execute in SQL and do not populate entity trackers.

On a left join, `matchingRight` means a present right row satisfying the
predicate. It includes an explicit presence guard, including when a nullable
field predicate could otherwise accept the artificial NULL row. `withRight()`
retains matched pairs; `withoutRight()` retains unmatched left rows. These
operations filter completed join results. Filtering the right input before
`leftJoin` instead changes which rows can match while retaining unmatched left
rows. These placements deliberately have different meanings.

## Predicates across both inputs

`matchingPair(quote fn(L, R): Bool)` filters joined pairs using mapped scalar
fields from either input. The quotation parameter position identifies the side;
joining a model to itself does not conflate its two sets of fields. Predicates
can combine field comparisons, supported captured scalar comparisons, and
Boolean operators with [computed scalar expressions](COMPUTED-EXPRESSIONS.md):
arithmetic, text concatenation and conditionals. Side predicates support the
same computed vocabulary. Values remain SQL parameters.

On a left join, `matchingPair` requires a present right row, just like
`matchingRight`. It uses `fn(L, R): Bool`, with both inputs available to the
predicate. Use `withoutRight()` to select absent rows separately. A predicate
accepting `R?` can instead use `matchingOptionalPair`, described below.
Pair predicates filter the completed stage at their declared position; they do
not change the join's ON condition or move ahead of a preceding limit.

The translator uses the existing scalar comparison semantics, including null
compensation for equality and its negation. Fields with the same scalar base
can compare even when only one is optional: NULL differs from every required
value, and two optional NULLs are equal. Neri's corresponding `T`/`T?` equality
rule is described in [LANGUAGE.md](../../docs/LANGUAGE.md).
Required numeric expressions support ordering comparisons. Required mapped
member paths remain available in chained inputs. Arbitrary navigation traversal
and method calls fail translation instead of executing filters on the client.
Computed Boolean guards use lazy CASE evaluation. On a left join, the implicit
presence guard for `matchingRight` and `matchingPair` also uses CASE, so checked
operations never receive artificial NULL operands from an absent right row.

## Optional right-row predicates

`LeftJoinedQuery.matchingOptionalPair(quote fn(L, R?): Bool)` lets one predicate
combine absent rows with conditions over present pairs:

```neri
query.matchingOptionalPair(quote do |customer: Customer, order: Order?|: Bool
  return order == null || order.total > customer.id
end)
```

The right parameter is optional. A null test on that parameter reads the
synthetic row-presence marker, not a nullable field. A present row with null
projected fields therefore remains distinct from an absent row. The predicate
decides whether absent rows pass; there is no implicit present-only filter.

The same scalar and Boolean expression vocabulary applies. Field reads on the
right require a guard establishing presence, such as `right != null && ...`
or `right == null || ...`; the compiler narrows the parameter for those reads.
The translator also validates this condition for manually constructed trees,
so a forged narrowed parameter node cannot bypass it. Unsupported or malformed
expressions fail before provider execution.
Conditional branches can establish presence in their selected arm as well.
Arithmetic and concatenation participate in the same guard validation; wrapping
a right member read inside a computed expression does not bypass it.

Optional predicates retain their position relative to filters, ordering,
offsets, and limits. Buffered results, counts, streams, and joined projections
share that pipeline. These predicates do not change the join's ON condition.

## Ordering and pagination

`orderByLeft(selector, descending)` and `orderByRight(selector, descending)`
set the primary ordering from a mapped scalar or computed expression. `thenByLeft` and
`thenByRight` append tie-breakers and require a preceding primary ordering.
Selectors are typed quotations; fields can be Int, Float, Bool, String, or
their optional forms. Null placement and string comparisons follow the
database's rules. Add sufficient tie-breakers when deterministic pagination is
required.

Computed ordering values occupy internal physical columns that retain their
types through pagination, filtering, projection and relation conversion. These
columns do not change the public joined result type. Later stages order by the
stored column rather than duplicating the quotation's bound parameters. A
computed right-side key on a left join yields NULL for an absent right row;
its expression is evaluated only for present rows.

An ordering selection's parameters precede those of its source subquery in SQL
occurrence order. Subsequent filters append their parameters; additional computed
ordering selections prepend theirs. PostgreSQL rendering rebases the nested
placeholders accordingly. Plans preserve checked-operation requirements through
all wrappers and reject providers missing that capability before dispatch.

`skip(n)` accepts 0 through 2147483647 and applies the offset at its position in the pipeline.
`take(n)` accepts 1 through 1000. Invalid limits and offsets remain failures
through subsequent operations. SQL stage limits and the buffered row bound
remain separate: a later filter or offset cannot accidentally enlarge a
previously bounded result. Outer stages and projections explicitly preserve
the effective ordering. Input-side orderings do not establish a joined-result
ordering; order the joined query itself when needed.
Composition traversal accepts at most 256 stages; database SQL complexity
limits may reject a smaller deeply nested pipeline.

`all()` requires a bound. `count()` counts the completed pipeline, including
offsets and limits. `stream` and `streamWith` visit the same pipeline without
requiring a bound. Existing synchronous streaming ownership, cancellation,
stop, and diagnostics contracts apply.

## Joined projections

`projectJoin(query, leftShape, rightShape, materialize)` combines two existing
`RowShape` values with `fn(A, B): V`. Shapes use `fieldShape` and
`combineShapes`, allowing nested result objects without fixed field counts.
`projectLeftJoin` accepts `fn(A, B?): V`; an absent right row contributes null.
Wrap nullable right-side fields in a result object when the application needs
to distinguish an absent row from a present row whose projected scalar is null.

Projection happens after the joined pipeline. The outer SQL selects only the
requested scalar leaves, plus an internal presence marker for a left join.
Unselected fields are not decoded or materialized. Every selected leaf on
both present sides is validated before any user result constructor runs.
For a matched right row, required selected fields retain strict NULL checks;
the presence marker alone determines absence. Result constructors run on the
client after decoding; filters and ordering run on the server.

Projection wrappers expose `compile`, `all`, `stream`, and `streamWith`.
They retain the join's shared-provider requirement and explicit provider
override. Results are untracked.

## Boundaries

Each join has two typed inputs; [CHAINED-JOINS.md](CHAINED-JOINS.md) describes
using a previous join as either input. Joined relations support
[global and grouped aggregation](AGGREGATES.md). Quoted constructors and
predicates over projected DTOs are outside this join contract.
SQLite is the execution adapter. PostgreSQL placeholder rendering is checked
as compilation output and does not establish a PostgreSQL execution adapter.

## References

The integrated `provider-contract` unit `join-composition` covers stage order,
left-join presence, projections, and streaming against SQLite. Core
join/projection and SQLite join contracts cover compatibility.

```sh
scripts/neri.sh run --project experiments/neri-data/provider-contract --unit join-composition
scripts/neri.sh run --project experiments/neri-data/provider-contract --unit join-composition --release
```

Primary sources checked on 2026-09-23:

- Microsoft, [EF Core complex query operators](https://learn.microsoft.com/en-us/ef/core/querying/complex-query-operators):
  typed joins translate into relational join conditions, with provider-dependent
  operator support. Neri's separate side predicates and shape API are its own
  interface, not a claim of EF Core expression coverage.
- SQLite, [SELECT processing, outer joins, ordering, and limits](https://www.sqlite.org/lang_select.html):
  unmatched rows are added before WHERE evaluation; explicit output ordering
  determines result order. These rules govern stage placement and presence
  handling here.
- SQLite, [CASE expressions](https://sqlite.org/lang_expr.html#the_case_expression):
  only the selected branch is evaluated. This underpins checked computed
  predicates and ordering over optional right rows.
- Microsoft, [query null semantics](https://learn.microsoft.com/en-us/ef/core/querying/null-comparisons):
  SQL comparisons may return NULL; explicit null compensation preserves
  Boolean equality and inequality. Neri applies its scalar type rules and
  presence markers rather than assuming EF Core's complete expression support.
- Microsoft, [nullable value types](https://learn.microsoft.com/en-us/dotnet/csharp/programming-guide/nullable-types/using-nullable-types):
  documents equality behavior for absent and present values. This is a
  reference for the Boolean contract; Neri retains its own optional scalar
  representation and does not adopt C#'s broader operator-lifting rules.
- Cheney, Lindley and Wadler,
  [A Practical Theory of Language-Integrated Query](https://homepages.inf.ed.ac.uk/slindley/papers/practical-theory-of-linq.pdf),
  ICFP 2013, introduction and section 5: inspectable quotations and query
  composition motivate a single-statement translation boundary. Neri uses
  staged SQL and client result constructors rather than implementing the
  paper's normalization calculus; its theorem does not prove this implementation.
