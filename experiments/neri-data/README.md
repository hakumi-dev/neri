# Neri Data

Neri Data provides generated, typed data access. The supported distribution and
release scope are documented in [Neri Data](../../docs/DATA.md).

[Filtered collection loading](FILTERED-NAVIGATION.md) applies typed predicates,
ordering and per-parent pagination in SQL before publishing navigations.

[Navigation graph registration](GRAPH-REGISTRATION.md) discovers related objects
from a generated `addGraph_<set>` call and registers write dependencies with
failure recovery before saving.

[Migration scaffolding](MIGRATION-SCAFFOLDING.md) writes reviewable up/down
operations and independent historical snapshots from mapping changes.

[Chained navigation loading](CHAINED-NAVIGATION.md) composes typed collection
and reference paths with recovery of earlier navigation assignments on failure.

[Typed table rebuilds](TABLE-REBUILDS.md) migrate existing non-key column types,
nullability and defaults with transactional copy and rollback.

[Concurrency tokens](CONCURRENCY-TOKENS.md) select original fields for tracked
UPDATE/DELETE checks and support explicit rebase or store-wins conflict recovery.

[Store-generated properties](GENERATED-PROPERTIES.md) support explicit insert
and update generation policies with typed SQLite RETURNING and rollback recovery.
It uses ordinary Neri declarations, typed quotations and `fields of T`, with a
SQLite provider for bounded reads, synchronous entity/projection/join streaming, and tracked
transactional writes. [Streaming and cancellation](STREAMING.md) describes the
untracked visitor API, cooperative tokens, deadlines, and native ownership.
Explicit tracked links coordinate related writes and generated keys; see
[relationship writes](RELATIONSHIPS.md) for the contract and capability boundary.
[Graph savepoint recovery](SAVEPOINTS.md) preserves earlier saves when a later
graph save fails inside an explicit transaction.
[Client cascade deletion](CASCADES.md) follows opt-in registered relationships
and stages dependent removals before their principal.
[Required orphan deletion](ORPHANS.md) stages dependent removal when an explicit
required client-cascade link is severed, while retaining its principal.
[Client nullification](CLIENT-NULL.md) retains optional dependents when their
principal is removed and saves their FK updates before the principal delete.
Typed set updates/deletes and provider execution summaries are documented in
[bulk writes and diagnostics](BULK-AND-DIAGNOSTICS.md).
[Grouped and global aggregates](AGGREGATES.md) compose scalar keys and typed
count, integer-sum, and integer/text-extrema results in one SQL query.
[Group composition](GROUP-COMPOSITION.md) adds typed aggregate predicates,
aggregate/key ordering, and pagination over the resulting groups.
[Cross-metric predicates](GROUP-PREDICATES.md) combine aggregate comparisons
with AND/OR/NOT and explicit nullable equality semantics.
The generated example maps `model::Customer` and
`model::Order`, including the `Customer.id` to `Order.customerId` relation.

The hand-written runtime is [`runtime.hk`](runtime.hk). The default sample is
[`generated-example/sample.hk`](generated-example/sample.hk). Generation and
generated-source loading are specified in
[`docs/GENERATED-SOURCES.md`](../../docs/GENERATED-SOURCES.md).

## Typed query API

Generated context members have the shared generic type `Query<T>`. The generator
does not create a query class, field class or snapshot class for each entity. A
generated context contains mapped `Query<model::Customer>` and
`Query<model::Order>` values backed by entity metadata and typed decoders.

```neri
use app_data
use model
use neri_data
use quotation

def example(): Void
  let db = database()
  let byId: quote fn(model::Customer): Int = quote do |customer: model::Customer|: Int
    return customer.id
  end

  let filtered = db.customers.where(active: true, nickname: null)
  let query = filtered.matching(quote do |customer: model::Customer|: Bool
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

Quotations also support [comparisons between mapped fields](COMPARISONS.md),
including nullable equality/inequality and ordered comparisons of required
integer fields. Both columns must have the same mapped scalar type.

Ordering uses typed quotation overloads:

```neri
query.orderBy(byId, true)
let byName: quote fn(model::Customer): String = quote do |customer: model::Customer|: String
  return customer.name
end
let byActive: quote fn(model::Customer): Bool = quote do |customer: model::Customer|: Bool
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
`Query<T>.all(provider)` and its scalar and shape projections materialize the selected rows. Without a query bound execution collects
a typed streaming read; `take(n).all(provider)` uses bounded buffered execution.
Unbounded materialization requires a provider with `StreamingReads` support.
Inspection previews at most a few materialized rows without rerunning the query.
`database(provider)` binds a borrowed provider to the generated query roots;
their transformations and projections retain it, so `all()` executes with that
provider. An explicit `all(otherProvider)` overrides it for that call and reads
without tracking. A
parameterless context still supports plan construction; execution without a
provider returns a failure.

Generated snapshots use the compiler field representation; key accessors retain
the declared key type:

```neri
snapshot_Customer(entity: model::Customer): fields of model::Customer
key_Customer(entity: model::Customer): Int
snapshot_Order(entity: model::Order): fields of model::Order
key_Order(entity: model::Order): Int
```

The generated relation method applies the mapped foreign key as a dependent
query filter. It does not join, lazy-load or mutate entities.

Relations with `collection` and `reference` metadata also expose an
`include_<relation>` helper. It populates ordinary entity arrays and inverse
references through batched reads, preserving tracked identity. See
[navigation loading](NAVIGATION-LOADING.md) for mapping, explicit reloads,
consistency, and failure boundaries.

`includeReference_<relation>` starts from dependents and populates principal
references, adding selected dependents to the inverse collection. Nullable
foreign keys represent optional relationships. See
[reference loading](REFERENCE-LOADING.md) for the partial collection contract.

`link_<relation>` connects typed tracked handles for ordered writes with required
or nullable integer/string foreign keys. `reassign_<relation>` changes a registered
principal and `unlink_<relation>` clears an optional relationship or removes a
required client-cascade orphan. `removeCascade_<set>` follows configured client
delete/null policies over registered links. See
[relationship writes](RELATIONSHIPS.md)
for generated-key propagation, rollback, and explicit-link lifetime.

## Generated metadata and provenance

The mapping at [`generated-example/mapping.json`](generated-example/mapping.json)
declares entity types, sets, tables, columns, keys and relations. Mapping JSON
uses canonical dotted names for namespace and entity identities, such as
`model.Customer`; Neri source spells the same type `model::Customer`.
Generated sources contain:

- `Database` members with `Query<T>` values and relation traversal methods;
- `EntityMapping` and `MappingColumn` metadata with canonical entity and field
  identities;
- typed entity decoders that return `DecodeResult<T>` and validate provider
  cells against declared scalar types and nullability;
- `snapshot_Entity` and `key_Entity` functions using `fields of T` and mapped
  key types;
- `schema_<Context>()` describing scalar columns, typed constant defaults and
  named indexes for [model migration planning](MODEL-MIGRATIONS.md).

The current generated project declares `entities`, `data` and `sample` units;
the `data` unit owns `database.hk` and the generated manifest, uses namespace
`app_data` with context `Database`, and publishes under `generated/`. Its
metadata names the `model.Customer` and `model.Order` mappings and the
`Customer.id` to `Order.customerId` relation.

The `tooling/data` project reads the mapping and referenced entity sources,
checks mapped members and scalar/nullability types, emits deterministic Neri
sources, and publishes stable filenames such as `Customer.entity.hk` and
`Order.entity.hk` directly under `generated/`. The manifest records mapping,
project and source inputs, output digests, and UTF-16 origin spans with semantic
identities. A verified sibling directory replaces the complete generated tree
atomically; hashes remain in metadata rather than public filenames. Reordering
the mapping does not rename existing entities. See
[generated sources](../../docs/GENERATED-SOURCES.md#neri-data-publication) for
ownership checks, obsolete generator-manifest rejection and filesystem
requirements.

Definitions resolve to generated declarations. The experimental
`neri/sourceOrigin` request resolves generated declarations back to entity source
spans. An unsaved input whose text differs from its recorded digest suppresses
the generated consumer model with `NR_GENERATED`. Retained-session configuration
identities include the generation manifest content. These APIs describe the
current manual source generator and manifest workflow; they do not claim a
generated pipeline for arbitrary projects.

## SQLite provider

Reference unit `sqlite` from `providers/sqlite/manifest.json` alongside the
generated application data unit. The adapter uses Neri C ABI imports to link
`sqlite3`; no C shim or SQLite CLI is required. The system must provide a
compatible SQLite 3 library for the native linker. Local verification uses the
macOS SDK's SQLite library; other platforms require their own native validation.

```neri
use app_data
use neri_data_sqlite
use console

def example(): Void
  match SQLiteProvider.open("app.sqlite")
    case SQLiteOpenResult.Opened(provider)
      let db = database(provider)
      let rows = db.customers.where(active: true).take(20).all()

      # Consume rows or its query failure before closing the borrowed provider.
      if !provider.close()
        console::println("Could not close SQLite")
      end
    case SQLiteOpenResult.Failure(message)
      console::println(message)
  end
end
```

`open` uses read-only access to an existing database file. `openWritable` opens
an existing file for reads and transactional writes, with foreign keys enabled.
`createWritable` also creates a missing file. Typed [schema migrations and
inspection](MIGRATIONS.md) manage schema versions and expose column metadata.
`close()` is explicit
and idempotent; the context does not own or close its provider. Use a provider
sequentially and close it after all associated queries. Statements and bound
text buffers are released after each execution, including failure paths.
The adapter checks SQLite storage classes before reading values and accepts
only integer 0/1 for mapped booleans. It preserves NULL and copies UTF-8 text
before advancing the statement.
Paths, generated SQL, and bound text parameters containing NUL bytes are
rejected. SQLite leaves expressions involving embedded NUL text undefined.

Generated contexts track entity reads and expose `saveChanges()` for inserts,
updates, and deletes collected through their query roots. `first`, `any`, and
`count` provide additional terminals; `select2` constructs typed DTOs from two
server-selected scalar fields. [Compositional shapes and joins](JOINS-AND-PROJECTIONS.md)
extend this with nested DTOs and typed inner/left equijoins. Nested SQL preserves filtering and ordering
after `take`; `skip`, secondary ordering, and mapped-row `distinct` compose
through the same query pipeline.

Contexts also expose explicit transactions and `clear()`. Query roots support
state and snapshot inspection, detachment, and explicit client-wins conflict
rebasing. SQLite savepoints keep individual saves retryable inside an outer
transaction; rollback reconciles both database changes and tracked baselines.

See [query composition and tracked changes](QUERY-AND-CHANGES.md) for the API,
transaction and conflict semantics, limitations, and research basis, and
[provider architecture](PROVIDERS.md) for extension and ownership contracts.

## Provider boundary and current limits

`Provider` supplies a dialect, advertises `BoundedTypedReads`, binds every
parameter, and returns raw rows. The runtime checks the plan's limit, selected
columns, scalar kinds and nullability, then materializes typed rows through the
generated decoder. The repository includes an in-memory contract provider and
a separately linked SQLite provider. PostgreSQL-style placeholders are
supported by the planner; a PostgreSQL connection adapter is not included.

Opt-in [generated integer keys](GENERATED-VALUES.md) use SQLite RETURNING and
restore temporary keys on rollback. The current contract has no automatic model
diff, chained join, navigation-driven cascade discovery, or generated-context
API for manual nested transactions. It supports scalar `Bool`, `Int`, `Float`, `String` and their
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

With the repository toolchain, verify the real SQLite adapter and generated
context integration in both native modes:

```sh
scripts/neri.sh run --project experiments/neri-data/providers/sqlite --unit contract
scripts/neri.sh run --project experiments/neri-data/providers/sqlite --unit contract --release
scripts/neri.sh run --project experiments/neri-data/provider-contract
scripts/neri.sh run --project experiments/neri-data/provider-contract --release
scripts/neri.sh run --project experiments/neri-data/provider-contract --unit persistence
scripts/neri.sh run --project experiments/neri-data/provider-contract --unit persistence --release
scripts/neri.sh run --project experiments/neri-data/concurrency-tokens --unit workers-contract
scripts/neri.sh run --project experiments/neri-data/concurrency-tokens --unit workers-contract --release
```

These contracts create isolated SQLite files through a test-only fixture unit.
Application reads use `open`; tracked writes use `openWritable` and `saveChanges`.

The scale probe is a bounded diagnostic, not a performance claim:

```sh
scripts/neri.sh run --project benchmarks/data --unit benchmark -- \
  "$PWD" "$PWD/scripts/neri.sh"
```

The probe reports generic specialization counts, generated bytes, and
diagnostics for each mapping size. Rerun it when changing generated declarations;
its output depends on the generator and compiler revision. It does not measure
database throughput or provider memory usage.

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
