# Neri Data

Neri Data is a bounded feasibility prototype for generated, typed data access.
It uses ordinary Neri declarations, typed quotations and `fields of T`; it has
no ORM or database driver. The generated example maps `model.Customer` and
`model.Order`, including the `Customer.id` to `Order.customerId` relation.

The hand-written runtime is [`runtime.hk`](runtime.hk). The default sample is
[`generated-example/sample.hk`](generated-example/sample.hk). Generation and
generated-source loading are specified in
[`docs/GENERATED-SOURCES.md`](../../docs/GENERATED-SOURCES.md).

## Typed query API

Generated context members have the shared generic type `Query<T>`. The generator
does not create a query class, field class or snapshot class for each entity. A
generated context contains mapped `Query<model.Customer>` and
`Query<model.Order>` values backed by entity metadata and typed decoders.

```neri
use app_data
use model
use neri_data
use quotation

def example(): Void
  let db = database()
  let byId: quote fn(model.Customer): Int = quote do |customer: model.Customer|: Int
    return customer.id
  end

  let filtered = db.customers.where(active: true, nickname: null)
  let query = filtered.matching(quote do |customer: model.Customer|: Bool
    return customer.id > 10
  end).orderBy(byId, true).take(20)

  let projected = select(query, intScalar(), byId)
  let plan = projected.compile()
end
```

`Query<T>.where(labels filters: fields of T)` resolves labels and value types
from the declared entity fields. Omitted labels produce no predicate; `false`
is an explicit Boolean parameter; `null` is available only for nullable fields
and produces `IS NULL`. Field arguments retain source evaluation order and the
pack retains source order for predicate and parameter binding.

`fields of T` exposes all public supported fields of the entity type. Mapping
membership is checked during `translateFields` and `translateQuotation`; a
field can be statically well-typed yet produce
`QueryFailure.UnsupportedCapability` when it is not mapped. Static field and
value typing does not promise successful SQL translation at compile time.

`Query<T>.matching(predicate: quote fn(T): Bool)` translates the supported
quotation tree. Captured values are typed and evaluated once. Nullable equality
is compensated for to preserve Neri's boolean semantics: a non-null capture
adds an `IS NOT NULL` guard and a bound equality, while a null capture becomes
`IS NULL`. See Microsoft's
[EF Core query null semantics](https://learn.microsoft.com/en-us/ef/core/querying/null-comparisons)
for the SQL distinction that motivates this boundary.

Ordering uses typed quotation overloads:

```neri
query.orderBy(byId, true)
let byName: quote fn(model.Customer): String = quote do |customer: model.Customer|: String
  return customer.name
end
let byActive: quote fn(model.Customer): Bool = quote do |customer: model.Customer|: Bool
  return customer.active
end
query.orderByString(byName)
query.orderByBool(byActive)
```

The overloads are named because method-level generic parameters are outside the
current Neri surface. A selector must resolve to a mapped field of the query's
entity; arbitrary callbacks, dynamic field names and string filters are not
translation APIs.

Projection is a shared generic module function:

```neri
select<T, R>(query: Query<T>, codec: ScalarCodec<R>, selector: quote fn(T): R): Projection<T, R>
```

`T` is inferred from the query and `R` from the scalar codec. `intScalar()`,
`boolScalar()`, `textScalar()` and their nullable variants provide strict
decoders and result-column metadata. `compile()` is the explicit plan terminal;
`all(provider)` is the provider boundary that executes a bounded typed read.

Generated snapshots use the compiler field representation; key accessors retain
the declared key type:

```neri
snapshot_Customer(entity: model.Customer): fields of model.Customer
key_Customer(entity: model.Customer): Int
snapshot_Order(entity: model.Order): fields of model.Order
key_Order(entity: model.Order): Int
```

The generated relation method applies the mapped foreign key as a dependent
query filter. It does not join, lazy-load or mutate entities.

## Generated metadata and provenance

The mapping at [`generated-example/mapping.json`](generated-example/mapping.json)
declares entity types, sets, tables, columns, keys and relations. Generated
sources contain:

- `Database` members with `Query<T>` values and relation traversal methods;
- `EntityMapping` and `MappingColumn` metadata with canonical entity and field
  identities;
- typed entity decoders that return `DecodeResult<T>` and validate provider
  cells against declared scalar types and nullability;
- `snapshot_Entity` and `key_Entity` functions using `fields of T` and mapped
  key types.

The current generated project declares `entities`, `data` and `sample` units;
the `data` unit owns `database.hk` and the generated manifest, uses namespace
`app_data` with context `Database`, and publishes under `generated/`. Its
metadata names the `model.Customer` and `model.Order` mappings and the
`Customer.id` to `Order.customerId` relation.

The `tooling/data` project reads the mapping and referenced entity sources,
checks mapped members and scalar/nullability types, emits deterministic Neri
sources, and publishes revision-named artifacts before atomically replacing the
generated manifest. The manifest records mapping, project and source inputs,
output digests, and UTF-16 origin spans with semantic identities. Obsolete owned
revisions are removed after the manifest switch.

Definitions resolve to generated declarations. The experimental
`neri/sourceOrigin` request resolves generated declarations back to entity source
spans. An unsaved input whose text differs from its recorded digest suppresses
the generated consumer model with `NR_GENERATED`. Retained-session configuration
identities include the generation manifest content. These APIs describe the
current manual source generator and manifest workflow; they do not claim a
generated pipeline for arbitrary projects.

## Provider boundary and current limits

`Provider` supplies a dialect, advertises `BoundedTypedReads`, binds every
parameter, and returns raw rows. The runtime checks the plan's limit, selected
columns, scalar kinds and nullability, then materializes typed rows through the
generated decoder. The repository's executable contract uses an in-memory
provider. No SQLite, PostgreSQL or other database driver is included, and this
prototype makes no production deployment claim.

The current contract has no change tracker, insert, update, delete, transaction,
migration or join API. It supports scalar `Bool`, `Int`, `String` and their
optional forms. Quotation calls, overloaded operators represented as calls,
indexing, casts, arithmetic and control-flow statements remain rejected with
`NR276`. A query without a limit fails when `compile()` evaluates it; valid
limits are 1 through 1000. Generated entity decoding reports missing columns
and strict NULL/type mismatches.

## Sources and focused commands

Run from the repository root with the local toolchain:

```sh
scripts/neri.sh run --project tooling/data --unit generator -- \
  "$PWD/experiments/neri-data/generated-example/mapping.json"
scripts/neri.sh run --project experiments/neri-data/generated-example --unit sample
scripts/neri.sh run --project experiments/neri-data --unit runtime-contract
scripts/neri.sh run --project . --unit data-generation-contracts -- "$PWD"
scripts/neri.sh run --project . --unit generated-source-contracts -- "$PWD"
```

The scale probe is a bounded diagnostic, not a performance claim:

```sh
scripts/neri.sh run --project benchmarks/data --unit benchmark -- \
  "$PWD" "$PWD/scripts/neri.sh"
```

The verified scale log `/tmp/neri-data-delivery-scale.log` records the following
generic binding sizes for generated entities:

| entities | generic instances | generated bytes | diagnostics |
| ---: | ---: | ---: | ---: |
| 2 | 42 | 5,899 | 0 |
| 3 | 54 | 8,799 | 0 |
| 4 | 66 | 11,699 | 0 |
| 8 | 114 | 23,299 | 0 |

At 16 and 32 entities, specialization reaches the 128-instance cap and reports
`NR222`. These figures bound the current prototype; they are not throughput or
memory benchmarks.

## Research basis

These primary sources motivate specific contracts; they do not establish
correctness for this implementation.

- Cheney, Lindley and Wadler, [A Practical Theory of Language-Integrated
  Query](https://doi.org/10.1145/2500365.2500586), ICFP 2013, describes
  quotations, normalization and conditions for translating composable queries.
  Neri Data implements a smaller explicit expression and descriptor vocabulary.
- Cheney, Lindley, Radanne and Wadler, [Effective
  Quotation](https://arxiv.org/abs/1310.4780), PEPM 2014, relates quotation and
  effect-based query representations. Neri Data does not inspect ordinary Neri
  callbacks.
- Microsoft, [Expression
  trees](https://learn.microsoft.com/en-us/dotnet/csharp/advanced-topics/expression-trees/)
  and [EF Core client versus server
  evaluation](https://learn.microsoft.com/en-us/ef/core/querying/client-eval),
  motivate an inspectable operation set and explicit translation failure.
- Microsoft, [EF Core query null
  semantics](https://learn.microsoft.com/en-us/ef/core/querying/null-comparisons),
  documents SQL three-valued logic and null compensation relevant to nullable
  equality and `IS NULL` translation.
- PostgreSQL, [Comparison functions and
  operators](https://www.postgresql.org/docs/18/functions-comparison.html),
  distinguishes ordinary comparisons from `IS NULL`; the nullable field API
  preserves that distinction.
- Mokhov, Mitchell and Peyton Jones, [Build Systems à la
  Carte](https://www.microsoft.com/en-us/research/wp-content/uploads/2018/03/build-systems.pdf),
  ICFP 2018, DOI `10.1145/3236774`, motivates explicit input inventories and
  content checks in the manual generation workflow.
- [ECMA-426 (2024)](https://tc39.es/ecma426/2024/) specifies mappings between
  generated and original source locations. Neri's independent manifest schema
  uses semantic identities and UTF-16 spans; it is not an ECMA-426 source map.

Rider completion and the language-service probes remain empirical acceptance
checks for the generated `Database`, `where`, `matching` and related members.
They do not establish a particular editor integration or database backend.
