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
| Queries | Arbitrary LINQ translation | Partial | Supported quotation vocabulary; no arbitrary callback translation |
| Queries | Set operations and correlated subqueries | Partial | Union/Concat/Intersect/Except preserve root tracking; arbitrary correlated projections remain limited |
| Queries | Tracking queries | Supported | Context identity and original snapshots |
| Queries | No-tracking query modes | Supported | Per-query tracking, detached reads and isolated identity resolution |
| Queries | Compiled queries and query caching | Partial | Reusable bound plans and compatible-context rebinding; no arbitrary compiled delegate cache |
| Queries | Checked PostgreSQL scalar arithmetic | Supported | Native checked Int/Float expressions and text concatenation; finite-value guard |
| Queries | Decimal numeric SQL operations | Partial | Exact required-Decimal equality, ordering, compare and add/subtract DSL; optional expressions and multiply/divide remain absent |
| Loading | Explicit collection/reference loading | Partial | Tracked complete, filtered, and chained loading; detached/no-tracking graph loading remains absent |
| Loading | Relationship fixup | Partial | Registered relationship descriptors; arbitrary collection edits do not infer every write link |
| Loading | Lazy loading | Partial | Cached explicit accessors; ordinary entity field reads do not load |
| Loading | Include query strategies | Partial | Key-batched loading; no general selectable single/split include execution contract |
| Loading | Async navigation loading | Partial | Owned collection/reference/many-to-many/lazy coordinators recover publication failures; general chained include async terminals remain absent |
| Persistence | Tracked CRUD and graph ordering | Partial | Dependency-ordered saves and provisional generated values; one graph cannot enroll distinct joins with the same initial assigned composite key |
| Persistence | Async tracked saves | Supported | Owned polling operations and cleanup |
| Persistence | Optimistic concurrency | Supported | Original tokens, explicit rebase and store-wins recovery |
| Persistence | Set-based update/delete | Supported | Typed assignments and atomic execution |
| Persistence | Async set-based update/delete | Supported | Native PostgreSQL and SQLite worker execution with context admission and transaction leases |
| Persistence | Transactions and savepoints | Supported | Sync/async provider-issued leases and checkpoint recovery |
| Persistence | External/shared transactions | Partial | Borrowed providers; no external native transaction enrollment contract |
| Persistence | Execution strategies/retries | Partial | Bounded sync/async replay for confirmed rollback after serialization, deadlock or contention; no reconnect/backoff policy |
| Persistence | Interceptors | Partial | Save veto/completion callbacks and provider diagnostics; command/query replacement pipelines remain absent |
| Models | Required/optional scalar properties | Supported | Int, Bool, Float, String, Binary, Decimal and optional forms |
| Models | Guid, date/time and native enum scalar families | Absent | Dedicated mapped Guid/DateTime/DateOnly/TimeOnly/native enum contracts remain absent; applications currently use explicitly converted supported scalars |
| Models | Primary and alternate tuple keys | Supported | Ordered assigned tuples; single generated Int key |
| Models | Generated values and concurrency tokens | Supported | Typed RETURNING with validation before commit |
| Models | One-to-many/reference relationships | Supported | Explicit tuple keys, loading and write links |
| Models | Many-to-many skip navigations | Partial | Explicit join navigation loading/link/unlink and graph enrollment; implicit join class synthesis remains absent |
| Models | Keyless entities and views | Partial | Read-only generated query roots and views; keyless navigation and keyed-view writes remain limited |
| Models | Inheritance mapping | Partial | Concrete subtype TPH with shared root tracking; TPT/TPC and nested hierarchy facets remain absent |
| Models | Owned/complex values | Partial | Required nested complex fields flatten into columns; optional/owned lifecycle and JSON remain absent |
| Models | Value converters/comparers | Partial | Strict Bool-to-Int conversion through decode, tracking, SQL and updates; arbitrary custom converters/comparers remain absent |
| Models | Shadow/indexer properties | Absent | Mapping targets ordinary declared fields |
| Models | Primitive collections and JSON mapping | Absent | No relational collection/JSON mapping contract |
| Models | Indexes and database defaults | Partial | Ordinary unique/index metadata and supported defaults; advanced facets are provider limited |
| Models | Check constraints/computed columns | Partial | Generated-value policies exist; general authored constraint/expression mapping is incomplete |
| Schema | Model snapshots and migration scaffolding | Supported | Immutable historical snapshots and typed up/down operations |
| Schema | Catalog/history/script management | Supported | Guarded history prefix and migration lifecycle |
| Schema | Async migration application | Supported | SQLite worker and PostgreSQL native guarded catalogs, history checks and rollback |
| Schema | Database reverse engineering | Partial | SQLite/public-schema PostgreSQL source and mapping scaffold for existing supported schemas; FK/index/check/collation/expression DDL facets need application review |
| Schema | Seeding | Partial | Immutable model seed snapshots and ordered parameterized catalog writes; cyclic dependencies and relationship rewrites alongside principal removal need explicit catalogs |
| Lifecycle | Context pooling | Supported | Bounded sequential owned leases, explicit application reset and generated context reset |
| Lifecycle | Provider diagnostics | Supported | Structured native failures and bounded operation summaries |
| Lifecycle | Provider breadth | Partial | SQLite and PostgreSQL; other EF Core providers are outside this comparison |

The baseline is [EF Core's stable release schedule](https://learn.microsoft.com/en-us/ef/core/what-is-new/).
Reference categories are [modeling](https://learn.microsoft.com/en-us/ef/core/modeling/),
[querying](https://learn.microsoft.com/en-us/ef/core/querying/),
[saving](https://learn.microsoft.com/en-us/ef/core/saving/),
[related data](https://learn.microsoft.com/en-us/ef/core/querying/related-data/),
and [migrations](https://learn.microsoft.com/en-us/ef/core/managing-schemas/migrations/).
Neri's current contracts are linked from [Neri Data](DATA.md).

Contract details: [model mapping](DATA-MODEL-MAPPING.md), [async operations](DATA-ASYNC.md), [context lifecycle](DATA-CONTEXT-LIFECYCLE.md), [many-to-many navigation](DATA-MANY-TO-MANY.md), and [seed data](DATA-SEEDING.md).

Query contracts are described in [query composition](DATA-QUERIES.md); live database source generation is described in [schema scaffolding](DATA-SCHEMA-SCAFFOLDING.md).
