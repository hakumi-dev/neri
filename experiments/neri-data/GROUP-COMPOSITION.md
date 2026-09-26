# Composing grouped results

Grouped results support filtering by a scalar aggregate, explicit ordering, and
offsets. Each modifier acts on the groups produced by the preceding modifiers.
All filtering, ordering, and pagination execute in one SQL statement; result
materializers still run only after decoding the selected output values.

```neri
let count = groupMetric(countRows<Order>())
let multiple = quote do |value: Int|: Bool
  return value > 1
end
let filtered = grouped.having(count.where(multiple))
let ordered = filtered.orderByAggregate(count.descending()).thenByKey()
let rows = ordered.skip(10).take(20).all()
```

`groupMetric(shape)` accepts a scalar `AggregateShape<T, V>` and preserves its
DISTINCT and per-aggregate FILTER modifiers. Its `where(quote fn(V): Bool)`
creates a typed group predicate; `ascending()` and `descending()` create typed
ordering terms. The metric may differ from the aggregates in the result DTO.
Combined aggregate shapes are not scalar metrics and fail before provider
execution. Result constructors do not provide implicit mappings for their fields.
[`selectGroupPairs` and `aggregatePair`](GROUPED-RELATIONS.md) provide certified
structural output that can become a relation for general scalar composition.

## Filtering and order

`having(predicate)` removes groups according to their aggregate value. Repeated
calls combine as successive AND filters. Conditions also support typed
cross-metric comparisons and AND/OR/NOT; see [GROUP-PREDICATES.md](GROUP-PREDICATES.md).
Scalar quotations support the existing scalar
comparison, captured-value, Boolean composition, and null rules: equality and
inequality for supported scalar types, and ordering comparisons for required
integers and Float values. For example, a nullable sum can be tested against null. Arbitrary
methods, computed expressions, and predicates over a materialized DTO remain
outside this contract.

`orderByAggregate(term)` replaces the current ordering;
`thenByAggregate(term)` adds a tie-breaker. `orderByKey(descending)` replaces
ordering with all grouping key leaves in declaration order, and
`thenByKey(descending)` appends those leaves. The direction defaults to ascending.
Composite key materializers do not change the scalar key ordering. Without an
explicit ordering modifier, groups retain ascending key-leaf order.

Null placement and text comparisons follow the database's ordering and collation
rules. To make pagination deterministic, include the grouping keys as the final
tie-breaker when aggregate values alone do not distinguish groups.

## Stage boundaries

The order of modifiers matters:

- `take(2).having(predicate)` filters only the first two groups.
- `having(predicate).take(2)` takes the first two matching groups.
- `take(2).orderByKey(true)` reorders only the first two groups.
- `orderByKey(true).take(2)` selects the first two groups in descending key order.
- `take(2).skip(1)` retains at most one group.

Earlier source-query filters, distinct, ordering, limits, and offsets continue to
act on aggregate input. Group modifiers act on the resulting groups. Group
output bounds do not limit the number of input rows aggregated. Buffered output
still requires a bound from 1 through 1000; invalid bounds and offsets remain
errors after later modifiers.

## SQL and binding contract

The compiler selects scalar metrics as hidden columns in a grouped derived
table. Group filters use `WHERE` over those aggregate values, implementing
HAVING semantics. Further subqueries preserve later filtering, ordering, and
pagination boundaries. This contract concerns query behavior; it does not
require a literal `HAVING` clause in generated SQL.

The terminal projection contains only the declared key and result aggregate leaves.
Hidden metrics are not decoded into the result, do not populate the tracker,
and do not change the result materializer's arguments. Ordering is stated again
at outer stages rather than relying on subquery row order. Structural relation
conversion retains typed hidden columns for downstream ordering.

Parameters follow their textual SQL positions: SELECT aggregate argument and FILTER values,
then source-query values, then outer group-predicate values. Hidden metrics with
FILTER expressions also contribute parameters to the grouped SELECT. SQLite
positional bindings and PostgreSQL numbered placeholders preserve these values
and their order. PostgreSQL remains compilation coverage; SQLite is the actual
execution adapter verified here.

Metric arguments and per-aggregate filters accept computed scalar quotations.
Checked-operation requirements survive hidden metrics even when every visible
result leaf uses only mapped fields. Providers must advertise the required
capability before dispatch. This does not extend the separate vocabulary for
predicates applied to the resulting scalar metric.

## Verification and references

```sh
scripts/neri.sh run --project experiments/neri-data --unit group-composition-contract
scripts/neri.sh run --project experiments/neri-data --unit group-composition-contract --release
scripts/neri.sh run --project experiments/neri-data/provider-contract --unit group-composition
scripts/neri.sh run --project experiments/neri-data/provider-contract --unit group-composition --release
```

Primary references checked on 2026-09-22:

- Microsoft, [EF Core GroupBy translation](https://learn.microsoft.com/en-us/ef/core/querying/complex-query-operators#groupby):
  describes aggregate predicates, aggregate ordering, and composition over scalar
  grouped results. These capabilities motivate this increment; the APIs are Neri's.
- SQLite, [SELECT processing](https://sqlite.org/lang_select.html):
  specifies grouping, HAVING, ordering, and LIMIT/OFFSET. It permits aggregate
  expressions used for filtering that are absent from the visible result.
- Okura and Kameyama,
  [Language-Integrated Query with Nested Data Structures and Grouping](https://www.logic.cs.tsukuba.ac.jp/~rui/flops.pdf),
  FLOPS 2020: studies grouping and aggregation in language-integrated queries and
  motivates explicit nested query structure for composition. Its formal scope
  excludes SQL NULL and restricts output shape; Neri's null, ordering, and
  pagination contracts are verified separately against SQL references and tests.
  This implementation does not claim that paper's normalization guarantees.

General predicates and projections over arbitrary application DTOs, exact
decimal and additional numeric types, aggregate input ordering, and grouped
collections remain outside this contract. Structural grouped
relations support scalar predicates, arithmetic and further projections.
