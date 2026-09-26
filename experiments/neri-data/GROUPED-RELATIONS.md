# Structural grouped relations

`selectGroupPairs(grouping, aggregates)` returns grouped results with type
`ProjectedPair<K, A>`: `first` contains the key and `second` the aggregate values.
`aggregatePair(left, right)` combines scalar or nested aggregate shapes into a
structural pair, using the same certified layout rules as `pairShape`.

```neri
let keys = fieldShape(intScalar(), quote do |order: Order|: Int
  return order.customerId
end)
let total = sumIntOrZero(intScalar(), quote do |order: Order|: Int
  return order.total
end)
let summaries = selectGroupPairs(groupBy(db.orders, keys),
  aggregatePair(countRows<Order>(), total)).asRelation()
let selected = summaries.matching(quote do |row: ProjectedPair<Int, ProjectedPair<Int, Int>>|: Bool
  return row.first > 0 && row.second.first > 1 && row.second.second > 20
end)
```

The grouped fields become ordinary SQL scalar columns. The relation accepts
scalar quotations for filtering and ordering and can participate in joins,
another grouping or `projectRelation`. Optional results retain their optional
type and require a null guard before arithmetic. Intermediate groups are not
materialized.

Existing `having`, aggregate/key ordering, `skip` and `take` stages remain before
the conversion. Later relation operators follow them: filtering after a group
`take(2)` filters only those two groups. Explicit limits remain in the SQL;
there is no implicit cap on groups used by another query. Buffered terminals
require a bound, while relation streaming permits unbounded output.

SQL, result layout and ordering metadata are compiled together. Hidden metrics
needed by later ordering remain relational columns without becoming pair fields.
Default group ordering is ascending key order. Parameters follow textual SQL
positions, and checked-operation requirements propagate through the whole plan.

Only certified keys and aggregate leaves can become relations. Arbitrary
`combineShapes`, `combineAggregates` and `selectGroups` callbacks remain
terminal-only, as do custom codecs that may transform scalar values. Returning
a pair from an arbitrary callback does not certify its field correspondence.
Quoted application constructors and grouped collections are unsupported.

## Verification and references

```sh
scripts/neri.sh run --project experiments/neri-data/provider-contract --unit query-composition
scripts/neri.sh run --project experiments/neri-data/provider-contract --unit query-composition --release
```

Primary references verified on 2026-09-23:

- Microsoft, [EF Core GroupBy translation](https://learn.microsoft.com/en-us/ef/core/querying/complex-query-operators#groupby):
  scalar aggregate results support subsequent query composition. Neri's API and
  limits are defined above.
- Okura and Kameyama, [Language-Integrated Query with Nested Data Structures
  and Grouping](https://www.cs.tsukuba.ac.jp/~kam/papers/flops2020-author-version.pdf),
  FLOPS 2020, sections 1–2: separating aggregation from output construction
  supports composition through subqueries. The calculus excludes SQL NULL;
  Neri verifies null, ordering and pagination behavior independently and does
  not claim the paper's normalization theorem.
- SQLite, [SELECT processing](https://www.sqlite.org/lang_select.html): derived
  tables, grouping, ordering and limits define the execution stages used here.
