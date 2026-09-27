# Composable join inputs

`Relation<T>` represents a typed SQL input with deferred compilation. A mapped
`Query<T>`, `JoinedQuery<L, R, K>`, or `LeftJoinedQuery<L, R, K>` exposes it through
`asRelation()`. Conversion does not execute a query or materialize rows.

Relations support `join` and `leftJoin` with another relation, a scalar key
codec, and two typed key quotations. The result is the existing joined query
API, so it can be filtered, ordered, bounded, projected, streamed, or converted
to a relation for another join. There is no separate API for each number of
inputs. The original `join(query, query, ...)` and `leftJoin(query, query, ...)`
functions remain available.

```neri
let pairs = join(db.customers, db.orders, intScalar(), customerId(), orderCustomerId())
let chain = pairs.asRelation().join(
  db.customers.asRelation(),
  intScalar(),
  quote do |pair: Joined<Customer, Order>|: Int
    return pair.left.id
  end,
  customerId()
).take(50)
```

The result type is `Joined<Joined<Customer, Order>, Customer>`. A subsequent
left join adds an optional right branch while retaining that nested left value.
Result reconstruction happens after execution of one composed SQL statement.
It does not issue a query per pair, populate trackers, or resolve entity identity.

## Paths and column layout

Selectors address required member paths such as `pair.left.id` or
`pair.right.customerId`. The full path of resolved member identities determines
the field, so self joins cannot confuse two occurrences of the same model.
Paths feed join keys and the existing mapped scalar predicates, ordering, and
result shapes. Join keys also accept the supported
[computed scalar expressions](COMPUTED-EXPRESSIONS.md) over these paths.
Arbitrary navigation traversal remains outside this contract.

SQL columns use independent positional aliases at each join boundary. Physical
result metadata preserves every column needed by the nested decoder, including
presence markers. Logical field types remain separate from physical nullability:
a required field in an absent optional branch occupies a nullable SQL column,
but a present branch still requires a correctly typed value.

Each left join has its own presence marker. An absent outer branch skips its
entire decoder; a present branch retains any internal absence markers. A matched
row whose nullable fields are all NULL remains a present object.

## Guarded optional paths

Scalar quotations can traverse optional intermediate objects after a null guard:

```neri
let amount = quote do |pair: LeftJoined<Customer, Order>|: Int?
  return pair.right != null ? pair.right.total : null
end
let total = pairs.asRelation().aggregate(sumInt(optionalIntScalar(), amount)).value()
```

Every optional ancestor needs a guard on the path where it is dereferenced.
Conjunction, disjunction, negation and conditional branches carry the corresponding
presence facts. A check on one join occurrence does not prove another occurrence
present, even when both have the same entity type. SQL uses lazy CASE to preserve
guards around checked arithmetic.

Optional-object checks use the path's synthetic presence marker. Nullable scalar
fields do not determine object absence. Each join prefixes the logical paths and
re-aliases marker columns with the physical row, including when a nested relation
is the optional right input of another join. An outer absence must be guarded
before inspecting an inner optional object.

The translator validates the full bounded quotation tree and its path guards
before emitting scalar SQL. Manually constructed trees cannot authorize an
unguarded path by labeling an optional member as required.

## Composition and execution

Each source keeps its filters, ordering, offsets, and limits inside its SQL
subquery. A limit on one source does not bound the multiplicity of a later join;
buffered output still requires its own explicit `take`. Ordering used to select
an input subset does not establish ordering for the new joined output.

Parameters from the source plans remain bound values. PostgreSQL rendering
renumbers placeholders across nested plans; SQLite provides execution coverage.
This does not establish a PostgreSQL execution adapter.

Join keys compile against each input's physical aliases inside the ON clause.
Parameters occur in left-source, right-source, left-key, right-key order.
Arithmetic, text concatenation and conditionals share the scalar translator's
type checks and expression budgets. Checked operations require the provider's
`CheckedScalarExpressions` capability, including when this join becomes another
join's input. Key failures are query errors; there is no client evaluation fallback.
SQL `=` remains the join comparison, so NULL keys do not match each other.
The database controls how often keys are evaluated for candidate rows.

```neri
let offset = 1
let shiftedCustomerId = quote do |customer: Customer|: Int
  return customer.id + offset
end
let shiftedOrderCustomerId = quote do |order: Order|: Int
  return order.customerId + offset
end
let rows = join(db.customers, db.orders, intScalar(), shiftedCustomerId,
  shiftedOrderCustomerId).take(50).all()
```

All leaves must resolve to the same provider for implicit execution. A missing
or conflicting provider in an inner relation propagates through the chain.
An explicit terminal provider override chooses one connection for the entire
SQL statement, according to the existing join contract.

## Boundaries

General quoted constructors, joins over arbitrary projected
DTOs and grouped collections are unsupported. Relations support
[global and grouped aggregation](AGGREGATES.md) over mapped and guarded optional paths. Query
complexity and database limits still apply to nested SQL.

Terminal projections can use [computed scalar expressions](COMPUTED-EXPRESSIONS.md)
over mapped and guarded optional paths, with the provider's checked-operation contract.

## References

Primary sources verified on 2026-09-23:

- SQLite, [SELECT processing](https://www.sqlite.org/lang_select.html), sections
  2.1 and 2.3: subquery inputs form relations and outer-join absence is introduced
  before WHERE filtering. These semantics govern input boundaries and presence.
- Microsoft, [EF Core complex query operators](https://learn.microsoft.com/en-us/ef/core/querying/complex-query-operators):
  describes typed joins, combinations of query sources, and provider-dependent
  translation. Neri's nested result and relation API are its own contracts.
- Cheney, Lindley and Wadler,
  [A Practical Theory of Language-Integrated Query](https://homepages.inf.ed.ac.uk/slindley/papers/practical-theory-of-linq.pdf),
  ICFP 2013, sections 1, 2 and 5: motivates composition with nested intermediate
  structures and a single SQL statement. Neri uses explicit relation plans and
  decoders; it does not implement the paper's normalization calculus or inherit
  its theorem.
