# Grouped and global aggregates

`AggregateShape<T, R>` describes a server aggregate over a mapped relational input.
`combineAggregates(left, right, materialize)` builds arbitrary typed result
shapes from aggregate leaves. `aggregateRows(query, shape).value()` evaluates
the shape across the source and returns `QueryValue<R>`.

`selectGroups(groupBy(query, keyShape), aggregates, materialize).take(n).all()`
evaluates the same aggregate shape for each key and returns untracked results. Keys use
the existing `fieldShape`, `expressionShape` and `combineShapes` APIs, so composite keys can contain
multiple mapped or computed scalar leaves without a fixed-arity grouping API.

```neri
let customerId = quote do |order: Order|: Int
  return order.customerId
end
let total = quote do |order: Order|: Int
  return order.total
end
let keys = fieldShape(intScalar(), customerId)
let totals = sumIntOrZero(intScalar(), total)
let grouped = selectGroups(groupBy(db.orders, keys), totals) do |customer: Int, amount: Int|: OrderSummary
  return new OrderSummary(customer, amount)
end
let rows = grouped.take(100).all()
```

## Joined and composed inputs

`Relation<T>.aggregate(shape)` and `Relation<T>.groupBy(keys)` accept the same
aggregate and key shapes over a deferred relation. Queries and inner/left joins
expose relations through `asRelation()`, including chained joins. Aggregation
executes one SQL statement without materializing or tracking intermediate pairs.

```neri
let pairs = join(db.customers, db.orders, intScalar(), customerId(), orderCustomerId())
let customer = fieldShape(intScalar(), quote do |pair: Joined<Customer, Order>|: Int
  return pair.left.id
end)
let amount = sumIntOrZero(intScalar(), quote do |pair: Joined<Customer, Order>|: Int
  return pair.right.total
end)
let summaries = selectGroups(pairs.asRelation().groupBy(customer), amount) do |id: Int, total: Int|: OrderSummary
  return new OrderSummary(id, total)
end
let rows = summaries.take(100).all()
```

Selectors resolve required mapped paths by member identity and parameter position
and can compose them with scalar expressions.
Join multiplicity contributes to each aggregate: repeated principal rows are
not implicitly deduplicated. A left join's unmatched row contributes to
`countRows`; required left-side fields remain selectable. Selectors through an
optional object require a null guard, as described in
[guarded optional paths](CHAINED-JOINS.md#guarded-optional-paths).

Source filters, ordering, offsets and limits complete before aggregation; an
output group limit does not restrict source rows. Internal sort columns remain
inside the source plan. Per-aggregate filters, DISTINCT, HAVING semantics and
group ordering work over the same relation. SELECT parameters precede source
parameters, and checked-operation requirements propagate from the source.

Execution uses the relation's shared provider, or an explicit terminal override.
Compilation builds the deferred source once to obtain its SQL and mapping.
The `aggregateRows(query, shape)` and `groupBy(query, keys)` factories keep their
Query-based signatures. Direct `GlobalAggregate`, `Grouping` and `GroupedAggregate`
constructors take `Relation<T>`; pass `query.asRelation()` when constructing them
directly.

## Scalar semantics

| Factory | Input | Result | Empty/all-null input |
| --- | --- | --- | --- |
| `countRows<T>()` | Rows | `Int` | `0` |
| `countNonNull(codec, selector)` | Scalar expression | `Int` | `0` |
| `sumInt(codec, selector)` | `Int` or `Int?` | `Int?` | `null` |
| `sumIntOrZero(codec, selector)` | `Int` or `Int?` | `Int` | `0` |
| `minInt` / `maxInt` | `Int` or `Int?` | `Int?` | `null` |
| `minString` / `maxString` | `String` or `String?` | `String?` | `null` |
| `average` | `Int`, `Int?`, `Float`, or `Float?` | `Float?` | `null` |
| `sumFloat` | `Float` or `Float?` | `Float?` | `null` |
| `sumFloatOrZero` | `Float` or `Float?` | `Float` | `0.0` |
| `minFloat` / `maxFloat` | `Float` or `Float?` | `Float?` | `null` |

Scalar aggregates ignore null inputs. `sumInt` preserves SQL sum semantics;
`sumIntOrZero` explicitly uses `COALESCE(SUM(...), 0)`. It does not substitute
SQLite's floating-point `total()` or convert an overflow into zero. Text
extrema and grouping follow the database's collation rules.

Float aggregates use approximate binary64 arithmetic. `average` translates to
`AVG`; `sumFloatOrZero` uses `COALESCE(SUM(...), 0.0)`. Non-finite Float results
fail strict decoding. These factories support the same distinct and per-leaf
filter composition as integer aggregates. See [FLOATS.md](FLOATS.md) for
storage and numeric boundaries.

Selectors accept the [computed scalar vocabulary](COMPUTED-EXPRESSIONS.md),
including checked arithmetic, text concatenation and conditional values.
For example, a selector returning `line.quantity * line.price` passed to
`sumInt(intScalar(), selector)` sums calculated inputs in SQL.
A conditional can return an optional scalar;
use the matching optional codec. Each operation validates its input and output
codec, including type, SQL kind and nullability. DISTINCT compares the calculated
input values. Integer expression overflow or division by zero fails the query;
nullable output and zero-default sums do not absorb those failures.

A global aggregate produces one result even when the source has no rows.
Grouping an empty source produces no groups. Null grouping keys compare equal
for grouping. Composite grouping uses the selected scalar leaves, not equality
of the materialized key object. Key materializers construct the result shape;
they do not translate transformations or alter SQL grouping equality.

## Per-aggregate distinct and filters

Scalar aggregate leaves accept `.distinct()` to remove duplicate input values
within each group or global aggregate. For example,
`countNonNull(intScalar(), customerId).distinct()` counts distinct non-null
customer IDs. This differs from `Query.distinct()`, which deduplicates the full
mapped source row before aggregation. Null inputs retain the scalar semantics
above. `countRows().distinct()` and `.distinct()` on a combined shape fail before
provider execution; apply distinct to each intended scalar leaf before combining.

`.where(quotedPredicate)` filters the input of an aggregate using SQL `FILTER`:

```neri
let paid = quote do |order: Order|: Bool
  return order.isPaid == true
end
let paidTotals = sumIntOrZero(intScalar(), total).distinct().where(paid)
```

Only rows where the predicate is true contribute to that aggregate. False and
unknown do not contribute. The filter preserves groups and sibling aggregates:
a group with no matching rows still returns count zero and nullable sum null.
Zero-default sum compiles as `COALESCE(SUM(DISTINCT column) FILTER (WHERE ...), 0)`.
Distinct and filtering can be applied in either order. Repeated filters combine
with AND, using lazy CASE when a computed predicate requires ordered guards.
A filter on a combined shape applies to every leaf and combines with
each leaf's own filters. Shapes are immutable and reusable; later modifiers
preserve an earlier invalid distinct operation.

These filters run over the completed source query, after its limits and offsets.
They accept computed Boolean quotations and bound parameters. SQLite evaluates
the aggregate argument only for rows accepted by its FILTER, so a filter can
exclude invalid arithmetic inputs. Parameters follow SQL occurrence order:
each leaf's argument, then its FILTER, then the next leaf, then the derived source.
SQLite positional bindings and PostgreSQL numbered placeholders preserve this order.

## Translation and execution

Each operation produces one SQL statement. The source is a derived table that
retains earlier filters, ordering, offsets, limits, and mapped-row distinct.
An output group limit does not cap aggregate input. An unbounded source can
contain more than 1000 rows. Source-query distinct applies to the full mapped
source row; per-aggregate distinct applies to its calculated scalar value.

Grouped buffered reads require an explicit bound from 1 through 1000. Repeated
`take` calls narrow that bound and retain earlier validation errors. Input
ordering selects input rows when combined with a source limit. Output groups
are ordered ascending by their scalar key leaves in declaration order; null
placement and text comparison follow the SQL dialect and database collation.
Typed group metrics add HAVING semantics, explicit aggregate/key ordering, and
offsets with preserved stage boundaries; see [GROUP-COMPOSITION.md](GROUP-COMPOSITION.md).

Providers receive typed result-column contracts. Decoding validates every key
and aggregate leaf in a row before invoking any result materializer. Result
constructors run on the client; grouping and aggregation run in SQL. Selectors
use mapped scalar paths and the checked expression vocabulary; invalid expressions
fail without evaluating them over downloaded entities. No entity tracker is
populated by these reads. Checked-operation requirements propagate from arguments
and filters, including metrics used only for group filtering or ordering.

SQLite is the verified execution adapter. PostgreSQL placeholder generation is
a compilation contract, not a claim of an installed PostgreSQL adapter.
Conditional expressions can compile for PostgreSQL; checked arithmetic and text
concatenation remain unsupported in that dialect and fail explicitly.
Exact decimal aggregates, aggregate input ordering, arbitrary
post-group DTO predicates/projections and grouping
collections remain outside this contract.

## Verification and references

```sh
scripts/neri.sh run --project experiments/neri-data --unit aggregate-contract
scripts/neri.sh run --project experiments/neri-data --unit aggregate-contract --release
scripts/neri.sh run --project experiments/neri-data/provider-contract --unit aggregates
scripts/neri.sh run --project experiments/neri-data/provider-contract --unit aggregates --release
scripts/neri.sh run --project experiments/neri-data/provider-contract --unit joined-aggregates
scripts/neri.sh run --project experiments/neri-data/provider-contract --unit joined-aggregates --release
scripts/neri.sh run --project experiments/neri-data/provider-contract --unit computed-aggregates
scripts/neri.sh run --project experiments/neri-data/provider-contract --unit computed-aggregates --release
```

Primary references checked on 2026-09-23:

- Microsoft, [EF Core GroupBy and aggregate translation](https://learn.microsoft.com/en-us/ef/core/querying/complex-query-operators#groupby):
  scalar grouping keys with aggregate projections translate to SQL. EF Core
  also supports aggregate predicates and ordering, covered by the typed group
  metric contract linked above.
- SQLite, [aggregate functions](https://sqlite.org/lang_aggfunc.html):
  specifies count, nullable sum/extrema, integer sum overflow, scalar DISTINCT,
  and per-aggregate FILTER behavior.
- SQLite, [`updateAccumulator` implementation](https://github.com/sqlite/sqlite/blob/master/src/select.c):
  checks the FILTER condition before evaluating the aggregate arguments.
  The native contract exercises this ordering with checked division.
- PostgreSQL, [aggregate expressions](https://www.postgresql.org/docs/current/sql-expressions.html#SYNTAX-AGGREGATES):
  defines DISTINCT and FILTER placement and their aggregate input semantics.
- SQLite, [SELECT processing](https://sqlite.org/lang_select.html):
  defines the single global aggregate row, null grouping equality, collation,
  and output ordering rules.
- Cheney, Lindley and Wadler,
  [A Practical Theory of Language-Integrated Query](https://homepages.inf.ed.ac.uk/slindley/papers/practical-theory-of-linq.pdf),
  ICFP 2013, introduction and section 1: motivates inspectable quotation,
  composition, and single-query translation. Its formal T-LINQ calculus
  explicitly excludes grouping and aggregation. This implementation does not
  claim the paper's theorem covers these operators.
- Ricciotti and Cheney,
  [Query Lifting: Language-integrated query for heterogeneous nested collections](https://arxiv.org/abs/2101.04102),
  ESOP 2021: studies a calculus with set and multiset semantics and translation
  to SQL. It motivates distinguishing duplicate-preserving input from explicit
  deduplication here; this implementation does not implement query lifting or
  claim the paper's correctness results.
