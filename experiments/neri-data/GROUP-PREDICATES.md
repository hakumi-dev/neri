# Predicates across aggregate metrics

`GroupHaving<T>` supports `.and(other)`, `.or(other)`, and `.not()`.
They combine typed group conditions without evaluating result DTOs or downloading
source entities. Conditions and metrics are immutable and reusable.

For example, given integer field quotations `requestedField` and
`fulfilledField` over `Order`:

```neri
let requested = groupMetric(sumIntOrZero(intScalar(), requestedField))
let fulfilled = groupMetric(sumIntOrZero(intScalar(), fulfilledField))
let count = groupMetric(countRows<Order>())
let large = quote do |value: Int|: Bool
  return value > 5
end
let incomplete = requested.greaterThan(fulfilled)
let needsReview = incomplete.or(count.where(large))
let rows = grouped.having(needsReview).take(20).all()
```

## Comparing two metrics

Metric comparison methods return a `GroupHaving<T>`:

| Method | Comparison | Supported output |
| --- | --- | --- |
| `equalTo(other)` | Equal | Matching scalar types, including nullability |
| `notEqualTo(other)` | Not equal | Matching scalar types, including nullability |
| `lessThan(other)` | `<` | Required `Int` or `Float` |
| `lessOrEqual(other)` | `<=` | Required `Int` or `Float` |
| `greaterThan(other)` | `>` | Required `Int` or `Float` |
| `greaterOrEqual(other)` | `>=` | Required `Int` or `Float` |

Both metrics must have the same entity input and result type. Compiled scalar
metadata must also agree. Combined DTO aggregate shapes remain unsupported as
metrics. Each metric retains its own DISTINCT and per-aggregate FILTER modifiers.
Nullable sums can be compared for equality; use the explicit zero-default sum
when zero is the intended empty-input value for integer ordering comparisons.

Nullable equality follows Neri's optional-value contract: two null values are
equal, exactly one null is unequal, and two present values use database scalar
comparison. Guarded SQL makes the equality result Boolean in all three cases.
Inequality negates that complete Boolean expression. Thus `equal.or(equal.not())`
retains every group, including groups with all-null aggregate inputs. Text
comparison still follows database collation rules.

## Composition and SQL

Boolean structure and parentheses are retained. Repeated `.having` calls still
apply successive AND filters; `.or` expresses alternatives within one condition.
Every branch is validated. SQL evaluation does not promise host-language
short-circuit evaluation, and an unsupported branch is rejected even if another
branch would be true.

The compiler selects hidden metric values in the grouped base, then filters those
values in an outer query. This implements HAVING semantics while preserving a
`having` applied after `take`, `skip`, or ordering. See
[GROUP-COMPOSITION.md](GROUP-COMPOSITION.md) for the stage contract.
Hidden metrics remain absent from the final result projection and decoder.

SELECT aggregate FILTER parameters are collected in visible-output order, then
hidden-metric traversal order. Source-query parameters follow them. Scalar
quotation values in outer group conditions follow the source parameters, in
left-to-right expression order. Comparing metric columns adds no comparison-value
parameters. SQLite positional parameters and PostgreSQL numbering preserve that
order. Reusing a condition can select its metrics more than once; the compiler
does not promise common-subexpression elimination.

Compilation accepts at most 256 visited condition nodes across a query's HAVING
stages and a maximum recursive depth of 64. Repeated references count on every
visit. Excessive complexity fails with `UnsupportedCapability` before provider
execution, preventing exponential expansion of repeatedly shared conditions.

## Verification and references

```sh
scripts/neri.sh run --project experiments/neri-data --unit group-predicate-contract
scripts/neri.sh run --project experiments/neri-data --unit group-predicate-contract --release
scripts/neri.sh run --project experiments/neri-data/provider-contract --unit group-predicates
scripts/neri.sh run --project experiments/neri-data/provider-contract --unit group-predicates --release
```

SQLite contracts cover nullable integer/text truth tables, integer ordering,
Boolean identities, independent aggregate filters, distinct metrics, empty
sources, and filters after pagination. Core contracts verify numbered SQL and
bound values, invalid metrics, and rejection before provider dispatch.
PostgreSQL coverage is compilation only; it is not a running second adapter.

Primary references checked on 2026-09-22:

- Microsoft, [EF Core query null semantics](https://learn.microsoft.com/en-us/ef/core/querying/null-comparisons):
  describes explicit compensation when translating host-language comparisons
  to SQL, including nullable equality and inequality.
- SQLite, [expression operators](https://www.sqlite.org/lang_expr.html):
  defines null propagation and Boolean operator behavior. Neri's optional-value
  equality is an explicit translation policy layered over SQL's three-valued logic.
- Leonid Libkin,
  [SQL's Three-Valued Logic and Certain Answers](https://homepages.inf.ed.ac.uk/libkin/papers/icdt15.pdf),
  ICDT 2015: distinguishes SQL evaluation from certainty-based interpretations
  of incomplete data. It motivates making null semantics explicit; Neri does
  not implement the paper's certain-answer evaluation scheme.

Arithmetic between metrics, predicates over arbitrary grouped DTO fields,
nullable ordered comparisons, and general computed projections are unsupported.
