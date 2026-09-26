# Joins and compositional projections

Computed scalar leaves compose with the projection shapes below; see
[COMPUTED-EXPRESSIONS.md](COMPUTED-EXPRESSIONS.md) for checked arithmetic,
concatenation, conditional expressions and provider requirements.

## Equijoins

`join(left, right, codec, leftKey, rightKey)` and `leftJoin(...)` connect two
mapped queries by typed, quoted scalar selectors, including the supported
computed expressions. Both keys must match
the codec's scalar type, including optionality. The join runs in one SQL
statement. Each input retains its own filters, ordering, offset, limit and
mapped-row distinct stages inside a derived table.

```neri
let customerId = quote do |customer: Customer|: Int
  return customer.id
end
let orderCustomerId = quote do |order: Order|: Int
  return order.customerId
end
let rows = leftJoin(db.customers, db.orders, intScalar(), customerId,
  orderCustomerId).take(100).all()
```

An inner result is `Joined<L, R>` with `left` and `right` entities. A left result
is `LeftJoined<L, R>` with an optional `right`: unmatched rows have null there.
A synthetic marker inside the right input distinguishes absence from a matched
row containing nullable fields. Right entities do not require primary-key
metadata for this distinction. Positional result aliases keep same-named
columns and self-joins unambiguous.

Join equality follows SQL `=` semantics: null keys do not match each other.
This rule is explicit; the API does not claim every LINQ provider's nullable
key behavior. The result preserves SQL multiplicity, so several matching right
rows produce several pairs. Generated decoders strictly validate present
entities. Join reads are untracked and do not perform navigation fixup or
identity resolution through the input contexts.

Buffered join output requires an explicit `take` between 1 and 1000. Repeated `take`
calls narrow the bound and an invalid bound remains a failure. Input queries
have no implicit 1000-row cap; their explicit limits still apply before the
join. Input ordering controls those selections, but does not guarantee an
order for joined output. `count()` counts the joined rows without materializing
entities and honors an optional output `take`; an unbounded count is allowed.
Output ordering and post-join filtering, including predicates over optional
right rows, are described in [JOIN-COMPOSITION.md](JOIN-COMPOSITION.md).
Typed [chained joins](CHAINED-JOINS.md) accept prior joined results as inputs.
The same relations support [global and grouped aggregation](AGGREGATES.md)
over required and guarded optional mapped paths.

Both inputs must use the same provider instance, or an explicit terminal provider
override must select the connection. A mismatch fails before provider execution. Parameters
from both inputs remain bound values; PostgreSQL compilation shifts right-side
placeholder numbers after the left input's parameters. SQLite is the real
execution adapter used by the integration contracts.

`stream(visitor, options)` delivers pairs synchronously through the shared
provider, without buffering or tracking. `streamWith(provider, visitor, options)`
selects an explicit provider. Unbounded streaming output is supported; limits
on either input do not cap join multiplicity. See [STREAMING.md](STREAMING.md)
for cancellation, delivery counts, and resource ownership.

## Arbitrary result shapes from mapped fields

`fieldShape(codec, selector)` describes one server-selected scalar field.
`combineShapes(left, right, materialize)` composes two typed shapes into another
typed result. Shapes can nest, so a DTO can contain more than two selected fields
without a separate `select3`, `select4`, or fixed-arity API.

```neri
let id = fieldShape(intScalar(), customerId)
let name = fieldShape(textScalar(), quote do |customer: Customer|: String
  return customer.name
end)
let summary = combineShapes(id, name) do |id: Int, name: String|: CustomerSummary
  return new CustomerSummary(id, name)
end
let rows = project(db.customers.where(active: true).take(20), summary).all()
```

The query selects only the shape's requested fields in its outer projection.
Inner stages may need the full mapped row to preserve earlier query operators.
Repeated field selections receive separate positional aliases. Each selected
cell decodes once; all fields in one row decode successfully before any nested
DTO materializer runs. Malformed later rows do not undo callbacks for earlier
valid rows, so materializers should construct values without external effects.

Materializers run on the client after selection and decoding. They are not
translated filters, joins or computed SQL expressions. Shapes preserve the
source query's operator order, provider selection, row bounds and mapped-row
distinct semantics. In particular, distinct source entities may project to
equal scalar or DTO values. Results remain untracked.

Scalar, two-field, and compositional shape projections also expose `stream`
and `streamWith` with the same decoded result types. Streaming permits unbounded
sources while retaining explicit input limits and strict decoding. Buffered
terminals retain their existing bound requirements.

Shapes select mapped scalar fields or computed expressions from `Query<T>` and either side of a join
through `projectJoin` and `projectLeftJoin`. Joined queries also support side
predicates, ordering, and pagination; see [JOIN-COMPOSITION.md](JOIN-COMPOSITION.md).
Shapes also define scalar and composite keys for [grouped aggregates](AGGREGATES.md).
For results that participate in further relational operations,
[`pairShape` and projected relations](PROJECTED-RELATIONS.md) provide a fixed
structural layout. Arbitrary quoted constructor bodies are outside this contract.

## Verification

```sh
scripts/neri.sh run --project experiments/neri-data --unit join-contract
scripts/neri.sh run --project experiments/neri-data --unit projection-contract
scripts/neri.sh run --project experiments/neri-data/provider-contract --unit joins
scripts/neri.sh run --project experiments/neri-data/provider-contract --unit joins --release
scripts/neri.sh run --project experiments/neri-data/provider-contract --unit projections
scripts/neri.sh run --project experiments/neri-data/provider-contract --unit projections --release
```

Contracts cover same-named columns, self-joins, nullable keys, present versus
absent right rows, composed input limits, input data exceeding 1000 rows,
provider mismatch, and PostgreSQL parameter numbering. Projection contracts
cover nested DTOs, repeated fields, strict nullable decoding, one codec call
per selected cell, and failure before DTO callbacks. Generated-context tests
verify that an invalid unselected scalar does not force entity materialization.

## Verified primary references

- Cheney, Lindley and Wadler, [A Practical Theory of Language-Integrated
  Query](https://homepages.inf.ed.ac.uk/jcheney/publications/cheney13icfp.pdf),
  ICFP 2013. Quotation and compositional query translation motivate retaining
  inspectable typed selectors and SQL stage boundaries. These APIs implement
  part of that design space and do not inherit the paper's normalization proof.
- Microsoft, [EF Core complex query operators](https://learn.microsoft.com/en-us/ef/core/querying/complex-query-operators):
  scalar key equality joins and optional right-side results provide the
  comparison surface. Provider translation capabilities differ.
- SQLite, [SELECT processing](https://www.sqlite.org/lang_select.html): derived
  inputs, ON filtering, left-row padding, multiplicity and outer ordering govern
  execution. The compiler retains each limited input as its own query stage.
- Microsoft, [client versus server evaluation](https://learn.microsoft.com/en-us/ef/core/querying/client-eval):
  final result construction is distinct from server-side predicate evaluation.
  Neri's materializer callbacks only construct decoded projection results.
