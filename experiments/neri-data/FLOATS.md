# Finite Float values

Neri Data maps `Float` and `Float?` entity fields to finite IEEE 754 binary64
values. Generated contexts decode, snapshot, update, and restore these fields
through the provider contract. `Float?` additionally accepts SQL NULL. Entity
keys remain required Int or String; foreign keys also support nullable forms
for optional relationships. Float entity and foreign keys are rejected.

`floatScalar()` and `optionalFloatScalar()` provide strict scalar codecs for
projections and aggregate inputs. Equality filters accept Float field packs,
quoted literals, and captured values. Required Float fields support ordered
quoted comparisons, `orderByFloat`, and `thenByFloat`. Nullable equality keeps
the existing Boolean NULL compensation; nullable ordered comparisons remain
unsupported.

The quotation ABI includes `FloatLiteral`, `Node.floatValue()`, and
`CaptureValue.Float`. The new Node constructor argument is trailing and
defaulted so existing positional construction retains its meaning. General
Float arithmetic inside database quotations remains outside the translator.
For a negative comparison constant, capture a precomputed Float binding;
the database translator currently rejects unary Negate nodes.

## SQLite and numeric boundaries

Parameters bind with `sqlite3_bind_double`. Reads inspect the storage class
before calling `sqlite3_column_double`: a mapped Float requires SQLITE_FLOAT;
an optional Float can also be NULL. INTEGER, TEXT, and BLOB cells fail without
implicit conversion. Declare physical columns with REAL affinity. SQLite
presents integral values in REAL-affinity columns as floating point even when
its on-disk optimization stores them as integers.

Neri Data rejects NaN and positive/negative infinity in filters, writes,
defaults, and non-null decoded Float results. The finite check uses ordered
comparisons on `value - value`, because Neri's language equality considers
two NaNs equal. This policy belongs to Data; the language itself supports
IEEE special values. SQLite may normalize the sign of zero. Snapshot equality
therefore treats positive and negative zero as equal.

`SchemaType.Float()` emits REAL. `SchemaDefault.Float(value)` requires a finite
value and a Float column. Typed defaults belong to authored migrations; they
do not add generated non-key value synchronization to contexts.

`average` returns `Float?` from Int or Float fields, including nullable inputs.
Float sum and extrema share the [aggregate contracts](AGGREGATES.md), including
empty/all-null inputs, DISTINCT, and FILTER. A returned infinity fails decoding.
These are approximate calculations; exact decimal arithmetic, currency codecs,
rounding policies, and numeric conversion mappings are outside this scalar contract.

## Verification

The provider fixture checks strict storage classes, REAL affinity, bound values,
typed projections, set-based and tracked writes, rollback, and special-value
rejection. The generated consumer combines authored Float migrations, mapping
generation, captured quotations, field packs, ordering, aggregates, nullable
snapshots, and retry after outer rollback.

```sh
scripts/neri.sh run --project tooling/data --unit generator -- "$PWD/experiments/neri-data/float-values/mapping.json"
scripts/neri.sh run --project experiments/neri-data/provider-contract --unit floats
scripts/neri.sh run --project experiments/neri-data/provider-contract --unit floats --release
scripts/neri.sh run --project experiments/neri-data/float-values
scripts/neri.sh run --project experiments/neri-data/float-values --release
scripts/neri.sh run --project experiments/neri-data --unit migration-contract
```

## Verified references

Primary sources checked on 2026-09-22:

- SQLite, [datatypes and affinity](https://www.sqlite.org/datatype3.html):
  REAL affinity presents integer values as floating point. This supports the
  strict storage contract for correctly declared mapped columns.
- SQLite, [binding](https://www.sqlite.org/c3ref/bind_blob.html) and
  [column access](https://www.sqlite.org/c3ref/column_blob.html): native double
  binding and storage inspection precede extraction without accessor coercion.
- SQLite, [floating point numbers](https://www.sqlite.org/floatingpoint.html):
  binary64 approximates real numbers and most fractional decimal values.
- SQLite, [aggregate functions](https://www.sqlite.org/lang_aggfunc.html):
  AVG returns floating point for non-null inputs; SUM can overflow to infinity.
- David Goldberg, [What Every Computer Scientist Should Know About Floating-Point Arithmetic](https://docs.oracle.com/cd/E19957-01/806-3568/ncg_goldberg.html),
  ACM Computing Surveys, 1991: rounding error and IEEE special values inform
  the explicit numeric boundaries. The finite-only Data policy is an engineering
  choice, not a correctness guarantee supplied by the paper.
