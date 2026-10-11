# Relational capability comparison

This matrix compares Neri Data with EF Core 10 relational application behavior
for SQLite and PostgreSQL. It describes capability coverage, not CLR API or
binary compatibility. A supported feature still obeys its provider's documented
limits. Partial means a usable subset exists; absent means no integrated public
contract is established. Distribution is separate from source capability.

| Area | Capability | Neri coverage | Contract or gap |
| --- | --- | --- | --- |
| Queries | Typed filters, ordering, pagination | Supported | Typed quotations and parameterized SQL |
| Queries | Scalar and row projections | Supported | Explicit typed codecs and composed shapes |
| Queries | Joins and grouped aggregates | Supported | Typed relational composition; bounded translation vocabulary |
| Queries | First, Single, Any, Count | Supported | Strict and optional cardinality terminals; sync and owned async variants |
| Queries | Global and named query filters | Supported | Model filter factories, named runtime filters and selective bypass across query stages |
| Queries | Raw SQL with typed results | Supported | Trusted mapped SQL roots and standalone typed/scalar sources; separately bound parameters |
| Queries | Query tags | Supported | Bounded cumulative SQL comment tags propagate through composition |
| Queries | Scalar translation vocabulary | Supported | Numeric, string, calendar, nullable and conditional expressions through quotations and typed builders |
| Queries | Set operations and correlated subqueries | Supported | Union/Concat/Intersect/Except preserve root tracking; typed correlated count, existence and first-row projections |
| Queries | Tracking queries | Supported | Context identity and original snapshots |
| Queries | No-tracking query modes | Supported | Per-query tracking, detached reads and isolated identity resolution |
| Queries | Compiled queries and query caching | Supported | Reusable bound plans, compatible-context rebinding and bounded context-local delegate caches |
| Queries | Checked PostgreSQL scalar arithmetic | Supported | Native checked Int/Float expressions and text concatenation; finite-value guard |
| Queries | Decimal numeric SQL operations | Supported | Exact required/optional comparisons, addition, subtraction, multiplication and explicitly scaled truncating division |
| Loading | Explicit collection/reference loading | Supported | Tracked and detached complete, filtered and chained loading with isolated identity scopes |
| Loading | Relationship fixup | Supported | Reference, collection and skip-navigation descriptors reconcile materialization and mapped relationship edits |
| Loading | Lazy loading | Supported | Explicit cached typed accessors with sync/async loading; ordinary access follows Neri field semantics |
| Loading | Include query strategies | Supported | Typed navigation descriptors, Include/ThenInclude chains and selectable single/split execution with documented bounds |
| Loading | Async navigation loading | Supported | Owned chained include, collection/reference, many-to-many and lazy operations share cancellation/deadline and publication recovery |
| Persistence | Tracked CRUD and graph ordering | Supported | Dependency-ordered saves, provisional composite identities and generated-key propagation with rollback |
| Persistence | Async tracked saves | Supported | Owned polling operations and cleanup |
| Persistence | Optimistic concurrency | Supported | Original tokens, explicit rebase and store-wins recovery |
| Persistence | Set-based update/delete | Partial | Typed atomic single-table writes and base-table TPT updates; TPT deletes and dynamic TPC targets are rejected |
| Persistence | Async set-based update/delete | Partial | PostgreSQL native and SQLite worker execution with context admission and leases; the same TPT/TPC restrictions apply |
| Persistence | Transactions and savepoints | Supported | Sync/async provider-issued leases and checkpoint recovery |
| Persistence | External/shared transactions | Supported | Borrowed native transactions, explicit owner outcome acknowledgement and checkpoint recovery |
| Persistence | Execution strategies/retries | Supported | Bounded sync/async replay after confirmed rollback with reconnect hooks, backoff, cancellation and deadlines |
| Persistence | Interceptors | Supported | Save callbacks and read/write/set command planning, result suppression and replacement across sync/async execution |
| Models | Required/optional scalar properties | Supported | Int, Bool, Float, String, Binary, Decimal and optional forms |
| Models | Guid, date/time and native enum scalar families | Supported | Canonical portable Guid, DateTime, DateOnly, TimeOnly, DateTimeOffset and TimeSpan codecs; explicit enum converters |
| Models | Primary and alternate tuple keys | Supported | Ordered assigned tuples; single generated Int key |
| Models | Generated values and concurrency tokens | Supported | Typed RETURNING with validation before commit |
| Models | One-to-many/reference relationships | Supported | Explicit tuple keys, loading and write links |
| Models | Many-to-many skip navigations | Supported | Explicit or synthesized join models, directed self relationships, tracked edits and loading fixup |
| Models | Keyless entities and views | Supported | Detached keyless roots with references to keyed principals; keyed read views can write through a separate mapped table |
| Models | Inheritance mapping | Partial | Nested TPH, TPT and TPC share root identity; subtype-only navigation/generated-value/index combinations and hierarchy seeds remain restricted |
| Models | Owned/complex values | Supported | Required/optional nested complexes, defining-navigation instance ownership, identifying owned collections and JSON storage |
| Models | Value converters/comparers | Supported | Explicit typed encoders/decoders and snapshot comparers; converted SQL values require the registered encoder identity |
| Models | Shadow/indexer properties | Supported | Typed getter/setter mappings and bounded reference-identity shadow storage with logical-property query access |
| Models | Primitive collections and JSON mapping | Supported | String/Int/Bool collections and nested complex JSON, mutation detection and typed JSON query expressions |
| Models | Indexes and database defaults | Partial | Authored defaults, descending/partial/expression indexes and PostgreSQL INCLUDE; additional provider facets use trusted migration SQL |
| Models | Check constraints/computed columns | Supported | Authored checks, computed expressions, collations and generated-value policies |
| Schema | Model snapshots and migration scaffolding | Supported | Immutable historical snapshots and typed up/down operations |
| Schema | Catalog/history/script management | Supported | Guarded history prefix and migration lifecycle |
| Schema | Async migration application | Supported | SQLite worker and PostgreSQL native guarded catalogs, history checks and rollback |
| Schema | Database reverse engineering | Partial | SQLite and public-schema PostgreSQL scaffolding includes FK/index/check/collation/generated expressions; unsupported definitions fail explicitly |
| Schema | Seeding | Supported | Immutable snapshots, parameterized writes, deferred cyclic references and retained-relationship rewrites before principal removal |
| Schema | Custom migration operations | Partial | Validated transactional single-statement SQL supports authored DDL/DML, triggers and functions; operations requiring execution outside a transaction remain unsupported |
| Lifecycle | Context pooling | Supported | Bounded sequential owned leases, explicit application reset and generated context reset |
| Lifecycle | Provider diagnostics | Supported | Structured native failures and bounded operation summaries |
| Lifecycle | Provider breadth | Supported | SQLite and PostgreSQL are the providers in this comparison |

The baseline is [EF Core's stable release schedule](https://learn.microsoft.com/en-us/ef/core/what-is-new/).
Reference categories are [modeling](https://learn.microsoft.com/en-us/ef/core/modeling/),
[querying](https://learn.microsoft.com/en-us/ef/core/querying/),
[saving](https://learn.microsoft.com/en-us/ef/core/saving/),
[related data](https://learn.microsoft.com/en-us/ef/core/querying/related-data/),
and [migrations](https://learn.microsoft.com/en-us/ef/core/managing-schemas/migrations/).
Neri's current contracts are linked from [Neri Data](DATA.md).

Contract details: [model mapping](DATA-MODEL-MAPPING.md), [async operations](DATA-ASYNC.md), [context lifecycle](DATA-CONTEXT-LIFECYCLE.md), [many-to-many navigation](DATA-MANY-TO-MANY.md), and [seed data](DATA-SEEDING.md).

Query contracts are described in [query composition](DATA-QUERIES.md); live database source generation is described in [schema scaffolding](DATA-SCHEMA-SCAFFOLDING.md).
