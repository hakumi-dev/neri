# Comparing mapped fields

`Query<T>.matching` and `AggregateShape<T, R>.where` accept typed quotations
that compare two direct mapped fields of the same entity. These comparisons
execute in SQL and can compose with existing captured-value predicates,
negation, AND, and OR.

```neri
let overLimit = quote do |order: Order|: Bool
  return order.total > order.limit
end
let rows = db.orders.matching(overLimit).take(20).all()
```

Both operands must have the same mapped scalar type, including nullability.
Equality and inequality support `Int`, `Float`, `Bool`, `String`, and their nullable
variants. Ordered comparisons `<`, `<=`, `>`, and `>=` require non-nullable
`Int` or `Float` fields. Fields must belong to the quotation's single entity parameter;
this does not add predicates over joined pairs or arbitrary computed operands.

## Null semantics

Nullable equality produces a Boolean even when either operand is null:

| Left | Right | `==` | `!=` and `!(left == right)` |
| --- | --- | --- | --- |
| null | null | true | false |
| null | value | false | true |
| value | null | false | true |
| non-null a | a | true | false |
| non-null a | distinct non-null b | false | true |

The SQL equality expression combines equality of two present values with the
case where both are null. Presence guards prevent an unknown SQL value from
escaping when exactly one operand is null. Inequality negates that complete
Boolean expression. This preserves the rule inside larger predicates and when
an aggregate uses the predicate as its own filter.

Comparison of non-null values follows the database's comparison, collation,
and storage rules. In particular, the SQL text comparison is not a promise of
Neri's ordinal text comparison under every database collation. Typed metadata
validation does not replace physical schema validation.

## Compilation and execution

Both columns are resolved through validated entity metadata and quoted as
identifiers. Column operands add no SQL parameters. Captured values and literals
retain their existing bound-parameter order, including PostgreSQL numbering.
Invalid identities, incompatible mapped types, and mismatched sources fail
before provider execution.

The shared predicate path also applies to source queries used by projections,
aggregates, streaming, and set-based update/delete. Earlier limits and offsets
remain separate SQL stages; a later comparison does not move before a limit.
The compiler recognizes the standard `String` equality operators in quotations;
arbitrary operator implementations and method calls remain unsupported.

SQLite is the verified execution adapter. PostgreSQL coverage verifies SQL
compilation and parameters, rather than a running PostgreSQL adapter. Computed
expressions, nullable ordered comparisons, mixed scalar types, post-join
predicates, and general client-side evaluation remain outside this contract.

## Verification and primary references

```sh
scripts/neri.sh run --project experiments/neri-data --unit comparison-contract
scripts/neri.sh run --project experiments/neri-data --unit comparison-contract --release
scripts/neri.sh run --project experiments/neri-data/provider-contract --unit comparisons
scripts/neri.sh run --project experiments/neri-data/provider-contract --unit comparisons --release
```

Core contracts check SQL operands, null guards, parameter order, and invalid
mapping rejection. SQLite checks the nullable truth table for integer, text,
and Boolean fields, required comparisons, negation, composed source stages,
aggregate filters, and set-based writes. These are behavioral checks, not a
formal correctness proof.

Primary sources checked on 2026-09-22:

- Microsoft, [EF Core query null semantics](https://learn.microsoft.com/en-us/ef/core/querying/null-comparisons):
  explains why translating Boolean equality and inequality into SQL needs
  explicit handling of nullable operands.
- SQLite, [expression operators](https://sqlite.org/lang_expr.html):
  specifies SQL null propagation and the AND/OR cases used by the guarded
  equality expression.
- Leonid Libkin,
  [SQL's Three-Valued Logic and Certain Answers](https://homepages.inf.ed.ac.uk/libkin/papers/icdt15.pdf),
  ICDT 2015, introduction: demonstrates how unknown comparison results affect
  compound predicates. This motivates an explicit semantic contract; Neri Data
  implements host-language optional equality, not the paper's certain-answer
  evaluation scheme for incomplete databases.
- Cheney, Lindley and Wadler,
  [A Practical Theory of Language-Integrated Query](https://homepages.inf.ed.ac.uk/slindley/papers/practical-theory-of-linq.pdf),
  ICFP 2013, sections 1–2: motivates typed quotation and composition followed by
  SQL translation. The null-compensation contract here is a Neri Data design
  checked against the database references and tests; it does not inherit the
  paper's normalization theorem.
