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

## Converted fields and mapped properties

`convertedField<T, V>(quote selector, encode: fn(V): Cell, conversionId)`
compares a converted model value through its registered encoder. The selector's
model type, storage type and encoder identity must agree with the mapping.
`equalTo`, `notEqualTo` and ordered comparisons produce a `ComputedPredicate<T>`
for `matchingComputed`. Equality compensates for SQL NULL. Ordered comparisons
require nonnullable Int, Float or TEXT storage; Decimal uses its dedicated exact
expression API. Primitive model fields with custom encoders require this explicit
API for comparisons, so a String encoder cannot silently compare plain text
against encoded text. Nullability remains part of the selector's model type.

`binaryField(quote selector)` and `optionalBinaryField(quote selector)` expose
the same equality operations for ByteBuffer fields. Creating a predicate copies
the captured buffer, so later mutations cannot change the query parameter.

`whereMappedCell(logicalName, value)` compares declared logical properties,
including shadow and indexer properties. The value uses the property's storage
type. Mapping validation resolves the logical name and checks the value; property
names are never inserted into SQL directly.

`executeUpdateMappedCell(logicalName, value)` writes one declared logical
property from a validated storage cell. It rejects primary-key changes and uses
the complete query pipeline to select the affected keys.

`UpdateSet<T>` combines typed SQL assignments for `executeUpdateSet` and
`executeUpdateSetAsync`. Use `setString`, `setOptionalString`, `setNumeric` and
`setOptionalNumeric` with the corresponding expression builders. `setConverted`
accepts a model value, its encoder and the exact mapped conversion identity;
it captures the encoded value once and copies binary storage. Converted targets
require this explicit encoder API. Assignments preserve SQL NULL and validate
required values, numeric bounds and finite Float results.

Set writes target one physical table. A TPT update may assign base-table fields
while selecting keys through predicates on the joined read source. Assignments
and computed values that depend on another physical table fail before SQL.
Segmented inheritance deletion and TPC mappings that select a table per entity
use tracked writes.

## String and calendar expressions

`stringField(quote selector)` and `optionalStringField(quote selector)` expose
`contains`, `startsWith` and `endsWith` literal predicates through
`matchingComputed`. Matching is case sensitive, and `%`, `_` and backslashes
remain literal parameter text. String functions require unconverted String
storage. `lower`, `upper`, `trim` and `substring(start, length)` compose before
`stringShape` or `stringLengthShape`; nullable equivalents use
`optionalStringShape` and `optionalStringLengthShape`. `coalesce(fallback)`
turns an optional string expression into a required expression.

Substring offsets count Unicode characters from zero. Start is bounded by
2147483646 and length by 2147483647; negative arguments fail compilation.
`trim` removes SQL spaces. Casing follows the provider: SQLite's built-in
functions transform ASCII, while PostgreSQL uses its configured locale.

`calendarField(quote selector)` reads native DateOnly, TimeOnly, UTC DateTime
and the local calendar parts of DateTimeOffset using its stored offset.
`utcCalendarField(quote selector)` reads DateTimeOffset's UTC instant;
its stored offset remains available when materializing the entity.
DateTimeOffset keys and foreign keys use normalized UTC storage and materialize
with offset zero, so their local calendar parts equal their UTC parts.
Available parts are year, month, day, hour, minute, second, millisecond, microsecond,
nanosecond, microsecondsWithinSecond, ticksWithinSecond, dayOfWeek (Sunday is
zero), and dayOfYear. Microsecond is the component within a millisecond (0..999);
nanosecond is the remaining 100-nanosecond tick component (0..900).
A date-only field
rejects time parts, and a time-only field rejects date parts.
`part.shape()` preserves SQL NULL, while `part.requiredShape()` requires a
nonnull result. Part comparisons produce predicates for `matchingComputed`.
These APIs require the native canonical storage mapping and encoder identity.

`intField`, `floatField`, `optionalIntField` and `optionalFloatField` build
typed numeric expressions. `toFloat()` uses the standard finite Int-to-double
conversion, including its precision limit for large integers.
`toIntTruncated()` truncates toward zero and rejects nonfinite or out-of-range
values before conversion. Optional casts retain NULL; `coalesce(fallback)`
returns a required numeric expression with a bound fallback. `shape()` selects
the expression. Scalar quotations also translate typed conditional expressions
to SQL CASE, preserving the order of query stages.

## Trusted composable SQL

`query.fromSql(sql, parameters)` supplies an unfiltered root with a trusted SQL
subquery exposing its mapped physical columns. `sqlQuery<T>` creates a typed
class query from an explicit decoder, optional mapping, and provider. `sqlScalar`
creates a scalar relation with a certified scalar codec and a result column
named `Value`:

Entity mutation methods require an unfiltered mapped root. User `fromSql`
sources reject those methods. Generated hierarchy and view mappings use
`fromMappedSql` to retain their physical write metadata while reading a composed
source. Writable mapped sources require the context tracker; read-only mappings
continue to reject writes. Bulk operations validate the physical layout even
when the query uses `asNoTracking()`.

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

`CompiledQuery.delegate` and `asyncDelegate` accept an explicit argument-to-parameter
binder and return reusable functions. `CompiledQueryCache<C,T>` adds a bounded
context-identity LRU around a query factory; each new context is checked once for
the same SQL, result columns, parameter kinds and provider dialect. Values that
change between calls belong in the binder. Eviction releases the cached context
and its plan; `clear` releases all entries.

## Exact Decimal expressions

`DecimalExpression<T>` provides exact Decimal fields and constants, addition,
subtraction, multiplication, explicit-scale division, comparisons, and ordering:

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
preserves it through later query stages. `multiply` is exact. `divideTruncated`
requires a scale from 0 through 4096 and truncates toward zero; division by zero,
invalid inputs and overlarge results fail. Local Decimal methods follow the
same rules. `optionalDecimalField`, `optionalDecimalValue` and
`selectOptionalDecimal` propagate NULL through arithmetic. Optional expressions
also provide `isNull`, null-safe `equalTo`, and `coalesce` with a required value.

`correlatedCount`, `correlatedAny` and `correlatedFirst` build typed scalar
projection shapes from a composed relation and matching outer/inner key
quotations. Source filters and ordering remain in SQL; a missing first row
produces NULL. Captures remain bound parameters.

Converted primitive collection fields support `stringCollection`,
`intCollection`, `boolCollection` and their optional variants. `contains` produces
a predicate for `matchingComputed`; `lengthShape` produces a nullable length.
JSON TEXT fields support `jsonField(...).property(name).index(index)` with
`textShape`, `intShape` and `boolShape`. Paths and values are parameters; missing
paths and JSON null produce NULL, while malformed JSON or wrong scalar types
fail. SQLite uses JSON functions and PostgreSQL uses strict JSONB paths.
`query.select(codec, quote selector)` also projects a complete converted model
value through an explicit `ScalarCodec`, including nullable JSON objects and
enums. The selector must name the mapped member directly and the codec must
declare its exact model type; computed quotations retain the scalar vocabulary.
