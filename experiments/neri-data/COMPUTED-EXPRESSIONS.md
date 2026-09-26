# Computed scalar queries

`expressionShape(codec, selector)` translates a typed scalar quotation into a
SQL selection. It composes with `fieldShape`, `combineShapes`, `project`,
`projectJoin`, and `projectLeftJoin`, and can define computed grouping keys.
The same translator accepts scalar key quotations in `join` and `leftJoin`,
including [chained relation inputs](CHAINED-JOINS.md).
The result codec must match the quotation's result type. User materializers run
only after the selected scalar leaves have decoded successfully.

```neri
let surcharge = 3
let total = expressionShape(intScalar(), quote do |order: Order|: Int
  return order.total * 2 + surcharge
end)
let results = project(db.orders.take(20), total).all()
```

Supported computed operations are same-type `Int` and `Float` addition,
subtraction, multiplication, division, and negation; required `String`
concatenation; and conditional expressions with a Boolean condition and matching
branch types. Mapped scalar fields, scalar literals, and immutable scalar
captures provide the leaves. Required nested member paths use the same mapping
identities as chained joins.

An optional result can contain a required value of the same base type or NULL.
The same rule applies to conditional branches; optional values cannot become
required results implicitly.

Quotation support is a language facility: the compiler records arithmetic,
canonical standard-library string concatenation, and conditional nodes without
executing them. A provider translator decides which quoted expressions it can
execute. This does not authorize arbitrary method calls or user-defined
operators inside quotations.

## Filters and ordering

Entity `Query<T>.matching` accepts comparisons over computed scalar values,
including arithmetic, concatenation and conditional results. Equality and
inequality preserve two-valued optional semantics: two missing values compare
equal, while a missing value differs from a present value. Ordered comparisons
require matching, required `Int` or `Float` operands.

```neri
let fee = 3
let minimum = 20
let eligible = quote do |order: Order|: Bool
  return order.total + fee > minimum
end
let priority = quote do |order: Order|: Int
  return order.total * -1
end
let results = db.orders.matching(eligible).orderBy(priority).take(20).all()
```

`orderBy`, `orderByFloat`, `orderByString`, `orderByBool` and their `thenBy`
counterparts accept computed quotations of their existing required scalar type.
A new primary order replaces the previous order at that stage; secondary orders
extend it. Filters or ordering after a limit or offset operate over the bounded
source stage. Entity distinct stages remain separate from terminal projections.

Computed Boolean expressions use lazy SQL CASE for `&&`, `||` and conditional
branches. A skipped branch does not execute checked arithmetic or concatenation.
Composed filters in the same stage use the same lazy conjunction when they
contain a computed predicate. This preserves left-to-right guards within that
stage; it does not establish a procedural evaluation order for the entire query.

## Parameters and stages

Literal and captured values are bound parameters. Selection parameters precede
source-subquery, predicate and ordering parameters in SQL occurrence order. Combining
shapes preserves this order across all leaves. Source filters, distinct,
ordering, offsets, and limits retain the query pipeline's stage boundaries.
Join ON expressions bind their left and right keys after both source plans.
Join equality uses SQL `=`: even computed NULL keys do not match each other.

Computed expressions execute inside SQLite's query engine. There is no implicit
fallback to fetching entity rows and evaluating the quotation in memory.
Constructing the final DTO with a `combineShapes` materializer remains an
explicit operation after scalar decoding.

## Checked SQLite operations

SQLite's built-in arithmetic is not sufficient to implement Neri's checked
integer contract: an overflowing integer operation can become a floating-point
value, and a later operation can hide that overflow. Each arithmetic node
therefore calls a registered, strictly typed SQLite scalar function.

Integer operations require INTEGER operands and reject signed 64-bit overflow,
division by zero, and minimum-integer division by minus one. Float operations
require REAL operands and finite inputs and results; division by zero fails.
This finite Float policy is the Neri Data contract, which is stricter than the
language's general IEEE binary64 arithmetic. Concatenation requires TEXT
operands and preserves their bytes. Unexpected storage types fail rather than
being coerced by SQLite.

Failures are query/provider errors, not successful NULL results. Consequently,
an invalid intermediate arithmetic value cannot be disguised as an optional
result. Conditional expressions use SQL CASE branch evaluation: an arithmetic
error in an unselected branch is not evaluated.

Checked functions are registered on each SQLite connection before it is exposed
to callers. Plans using checked operations carry a capability requirement; a provider that does
not advertise `CheckedScalarExpressions` is rejected before execution. The
PostgreSQL placeholder renderer does not establish support for these functions
or a PostgreSQL execution adapter.

Requirements from computed source predicates and ordering survive nested query
stages, bulk target selection, relation joins and aggregate wrappers. Aggregate
arguments, per-aggregate filters and hidden group metrics carry the same
requirements. Providers
must satisfy those requirements before any read, stream or write dispatch.

## Boundaries

This API produces scalar or DTO projections and grouping keys. The scalar
translator also powers [computed bulk assignments](BULK-AND-DIAGNOSTICS.md),
whose final values are validated before writing. It does not establish joins
over arbitrary projected DTOs, quoted constructors, or
aggregation over arbitrary projected DTOs. [Aggregate selectors and filters](AGGREGATES.md)
accept this vocabulary over mapped relational inputs, including joined inputs.
[Joined predicates and ordering](JOIN-COMPOSITION.md)
accept computed scalar expressions with explicit right-row presence semantics.
Joined ordering accepts optional scalar keys; entity ordering retains its required
scalar signatures. Paths through optional intermediate objects require guards
for every optional ancestor; see [guarded optional paths](CHAINED-JOINS.md#guarded-optional-paths).
Arithmetic requires non-null operands at the expression where it is evaluated.

Scalar quotations accept at most 256 nodes, 512 reachable-node visits, and a
65,536-byte SQL expression. Validation also applies to manually constructed
trees and rejects cycles, excessive repeated subtrees and invalid child types.

## Verified references

Primary sources checked on 2026-09-23:

- Microsoft, [Client vs. Server Evaluation](https://learn.microsoft.com/en-us/ef/core/querying/client-eval):
  distinguishes translatable query expressions from explicitly permitted final
  client projections. Neri's explicit materializer API has its own contract.
- Microsoft, [Query null semantics](https://learn.microsoft.com/en-us/ef/core/querying/null-comparisons):
  SQL's three-valued comparisons need compensation to preserve language Boolean
  results. Neri emits null-aware equality for computed optional operands.
- Cheney, Lindley and Wadler, [A Practical Theory of Language-Integrated Query](https://homepages.inf.ed.ac.uk/slindley/papers/practical-theory-of-linq.pdf),
  ICFP 2013, sections 2 and 5: quotations, scalar expressions and compositional
  translation motivate this design. Neri does not implement the paper's full
  calculus or inherit its normalization theorem.
- SQLite, [SQL expressions](https://sqlite.org/lang_expr.html), sections 2, 4 and
  7: numeric promotion, bound parameters, and lazy CASE evaluation.
- SQLite, [function registration](https://sqlite.org/c3ref/create_function.html),
  [value inspection](https://sqlite.org/c3ref/value_blob.html), and
  [function results](https://sqlite.org/c3ref/result_blob.html): connection-local
  callbacks, storage-class checks, typed results and statement errors underpin
  the checked operator implementation.
