# Typed query policies and reusable plans

Queries preserve typed filters, ordering, pagination, and projections as ordered
SQL stages. `First`, `Single`, `Any`, and `Count` semantics are exposed through
`first`, `single`, `any`, and `count`. `first` fails on an empty result; `single`
requires exactly one row. `firstOrDefault` and `singleOrDefault` return null for
an empty result. Single-row checks inspect row cardinality before publishing
entities to the tracker. A decoded nullable scalar value remains a valid row.
The corresponding `*Async` methods return an owned `AsyncValue<T>` operation.
Poll its `AsyncValuePoll` until completion and close it through `using`.

## Query filters, tags, and tracking

`withGlobalFilter` adds a default root filter. `withNamedGlobalFilter(name,
predicate)` adds or replaces a named root filter. Configure filters before adding
query stages. Root filters apply once, before user pagination and later filters.
`ignoreQueryFilters()` disables all default filters throughout the query;
`ignoreQueryFilters(["Tenant"])` disables selected named filters while retaining
unnamed filters. Query configuration returns a new query and preserves the
original root.

`tagWith` appends cumulative SQL line comments. Each input line receives a
comment prefix, including lines containing SQL syntax. Tags are literal SQL
comments; tag values are independent of bound query parameters. Each tag is
limited to 4096 bytes and cannot contain NUL.

`asTracking` uses the context tracker. `asNoTracking` materializes separate
entities without context enrollment. `asNoTrackingWithIdentityResolution` uses
a fresh resolver for each materialization, so repeated keys share one entity
within that result and leave the context tracker unchanged. Identity resolution
requires tracker metadata. Keyless mappings remain untracked. Buffered entity
set operations preserve the left query's tracking policy. Streaming visitors
materialize detached rows.

## Trusted composable SQL

`query.fromSql(sql, parameters)` supplies an unfiltered root with a trusted SQL
subquery exposing its mapped physical columns. `sqlQuery<T>` creates a typed
class query from an explicit decoder, optional mapping, and provider. `sqlScalar`
creates a scalar relation with a certified scalar codec and a result column
named `Value`:

```neri
let values = sqlScalar(
  intScalar(),
  "SELECT id AS \"Value\" FROM customers",
  new ParameterValues(),
  provider
).matching(quote do |value: Int|: Bool
    return value > 1
  end).orderBy(quote do |value: Int|: Int
    return value
  end).take(10)
```

Use the provider's parameter syntax and pass values separately. SQL strings are
limited to 65536 bytes and cannot contain NUL. PostgreSQL numbered parameters
are rebased during composition while quoted strings, quoted identifiers,
comments, and dollar-quoted bodies are preserved. The SQL must form a valid
subquery and expose the declared result shape. Providers admit reads through
their read execution contract. SQL is a trust boundary: a SELECT can invoke
database functions with effects. This API does not sandbox authored SQL.

## Set operations and compiled queries

`unionWith`, `concat`, `intersect`, and `except` compose compatible queries or
relations in SQL. `concat` retains duplicates; the other operations compare
complete mapped rows. Inputs retain their own filters and pagination. Hidden
ordering columns are excluded from duplicate comparison. Apply final ordering
after the set operation. Both inputs must have the same typed row layout,
compatible physical scalar storage, provider, and context when bound. Certified
scalar projections can also be converted with `asRelation` and composed.
Tracking set operations additionally require the same physical entity source.
Detached inputs can combine compatible rows from different sources.

`query.compileQuery()` creates a reusable `CompiledQuery<T>` for the provider's
dialect. `bind(parameters)` replaces captured values after checking the exact
parameter count and scalar types. Binary values are copied. Reads reuse SQL,
result metadata, and the decoder; default values remain unchanged.
`bindTo(compatibleQuery)` associates the shape with another query's decoder,
provider, context, captured defaults, and tracking policy. It translates the
target once to verify identical SQL and result/parameter shape. Subsequent reads
reuse that translated plan. Bound context lifecycle checks remain active.

## Exact Decimal expressions

`DecimalExpression<T>` provides exact required Decimal fields and constants,
addition, subtraction, comparisons, and ordering:

```neri
let amount = decimalField(quote do |item: Item|: Decimal
  return item.amount
end)
let adjusted = amount.add(decimalValue<Item>(increment))
let totals = db.items.take(10).selectDecimal(adjusted)
let selected = db.items.matchingDecimal(adjusted.greaterThan(amount))
let ordered = db.items.orderByDecimal(adjusted).take(10)
```

Comparisons include `equalTo`, `lessThan`, `lessThanOrEqual`, `greaterThan`, and
`greaterThanOrEqual`. `matchingDecimal` and `orderByDecimal` also accept a
relation. `selectDecimal` produces a canonical Decimal projection that can be
composed as a relation. Fields and results must contain canonical decimal TEXT;
invalid text, required NULL, or results exceeding 4096 digits fail. Expressions
are limited to 128 nodes. Ordinary Decimal methods execute in Neri; these
explicit expressions execute in SQL.

SQLite uses exact Decimal callbacks. PostgreSQL validates canonical TEXT,
calculates through unconstrained NUMERIC, and returns canonical TEXT. Neither
path converts through Float. Ordering uses a binary decimal sort key and
preserves it through later query stages. Required Decimal expressions support
addition and subtraction; optional Decimal arithmetic, multiplication, and
division have no SQL expression contract.
