# Neri Data

Neri Data provides generated, typed relational access through the `neri_data`
library and a SQLite adapter in `neri_data_sqlite`. Applications define ordinary
Neri entities and a mapping file; generation produces a typed context, entity
decoders, relationship helpers and schema metadata. Query quotations use the
compiler's typed expression trees described in [Quotations](QUOTATIONS.md).

## Distribution

The toolchain includes the runtime source project at
`share/neri/data/manifest.json` (unit `runtime`) and its SQLite source project at
`share/neri/data/sqlite/manifest.json` (unit `sqlite`). The generation commands
`neri-data-generate` and `neri-data-migration` use the standard library from their
own installed toolchain. Keep those commands and the Data source projects from
the same release.

The installed Data package is verified on Apple silicon with macOS 15 or newer
and LLVM 22.1.8. This release gate does not establish installed Data support on
other targets.

An application references the source projects using ordinary relative project
references. To keep the application portable, copy the complete
`share/neri/data` directory into its vendor directory and reference those copied
manifests. Preserve the relative `sqlite` directory. Generated source remains
application-owned output; regenerate it when the mapping or generator changes.

The same directory includes an Ito `package.json` exporting the `runtime` and
`sqlite` library units. Add the installed directory as a local dependency:

```sh
neri-data-generate /path/to/app/data/mapping.json
ito --project /path/to/app add ../toolchain/share/neri/data
ito --project /path/to/app build
```

The dependency path passed to `ito add` is relative to the application directory;
this example places `app` and `toolchain` beside each other under `/path/to`.

Include the entity and generated source directories in the application's
`package.json` `neri.sources`, alongside its other application sources, for
example `["app", "data/entities", "data/generated"]`. Keep the mapping's Neri
project focused on its entity declarations so generation can run before those
generated files exist. Ito supplies both exported libraries to the application;
it does not run the Data generator. Regenerate first when mapping inputs change.
Installed Data metadata and copied source files use the packaged Neri manifests
as their source inventory, including the owning `SQLiteSession` resource.

The packaged `examples/data-consumer` project exercises installed generation and
SQLite persistence. Its mapping and entity declarations provide a minimal
starting point. SQLite requires the platform's native SQLite 3.37.2 or newer
library and its exported C API; the provider
checks capabilities such as window functions on the actual open connection.

In a writable copy of the example and `share/neri/data`, preserving their
relative package layout, generate the context and scaffold its first migration
with:

```sh
neri-data-generate examples/data-consumer/mapping.json
mkdir -p examples/data-consumer/scaffold
neri-data-migration examples/data-consumer/mapping.json empty initial scaffold/initial
neri build --project examples/data-consumer/manifest.json --unit consumer --release
```

`empty` selects an empty historical schema. For the next migration, supply the
previous scaffold's `snapshot.json` in its place. The output directory must be
new, under an existing parent; the command never overwrites authored migration
source. Each scaffold contains `migration.hk`, `snapshot.json` and a completion
marker `scaffold.json`. Review the up/down operations and data-loss flags before
applying them. Scaffolding does not open a database. The application supplies and
owns the SQLite provider, applies a selected `MigrationSet`, and closes the
provider when finished. Generated source is published at stable paths such as
`generated/Account.entity.hk`; see [generated-source publication](GENERATED-SOURCES.md#neri-data-publication).

## Supported behavior

- Typed filters, scalar projections, ordering, pagination, joins and aggregates
  compile to parameterized SQL. Unsupported expression shapes fail explicitly.
- Generated contexts track entity identity and original values. Saves use
  transactional recovery, generated-value refresh and optimistic concurrency
  checks. Explicit transactions and savepoints preserve retryable state.
- Collection and reference loading compose through typed navigation paths.
  Filtered collection loading applies ordering and pagination independently for
  each parent. Reloaded membership follows database values while existing local
  scalar edits survive identity reuse.
- Graph registration discovers mapped navigations and orders related writes.
  Explicit relationship operations support reassignment, configured client
  cascades, orphan removal and nullable foreign-key updates.
- Mapping snapshots and migration scaffolding produce reviewable up/down source.
  SQLite applies supported schema changes transactionally and rejects unsupported
  dependencies before rebuilding tables.
- Synchronous streaming supports explicit stop, cooperative cancellation and
  deadlines. Provider diagnostics report completed operations without recording
  parameter values.

## Bounds and compatibility

The bounded buffered path requires an explicit limit of at most 1,000 rows. Navigation loads
have a separate explicit total bound of at most 10,000 children and batch at most
50 parent keys per child statement. Query `skip` and `take` inside a filtered
include apply to each parent's children. Collection loading stages publication
until all batches succeed; chained loading restores earlier navigation changes
after a later failure. Newly materialized tracker entries can remain.

Loaded-state metadata, automatic context-wide relationship fixup and lazy loading
are outside this release's loading contract. Callers explicitly reload the
selection they need. Multiple queries share a consistent database snapshot only
when the caller selects suitable transaction isolation.

The initial scalar mapping supports the documented integer, Boolean, text and
finite floating-point forms, including supported nullable variants. Primary keys
are single integer or required text fields. Composite/alternate keys, exact
decimal, arbitrary application-constructor query translation, asynchronous I/O
and additional real database adapters remain outside this release's scope.
PostgreSQL placeholder rendering is a SQL compilation contract, not a PostgreSQL
database adapter.

Neri Data is an explicit relational API for this release. Its supported
surface is defined here and does not imply feature parity with Entity Framework
Core.

`Query<T>.all()` and its scalar and shape projections materialize the selected rows. Without an explicit query bound, execution
collects the provider's typed streaming read; providers must support
`StreamingReads`. `take(n).all()` retains the bounded buffered path. Materializing
all rows consumes memory proportional to the result; `stream(...)` visits rows
without retaining the full result. Console inspection limits the visible preview,
independently of how many rows the query returns.
Joined and grouped query surfaces retain their explicit materialization bounds.

`neri_data_sqlite::SQLiteConnection(path)` is a lazy provider for an existing
writable database. Standalone operations acquire and close their own connection.
An explicit transaction retains one connection until commit, rollback or `close()`.
Opening failures remain structured operation results. Application configuration
and construction of generated contexts belong to the application; Sumi supplies
the interactive frontend, while Neri sessions inspect returned values.

For a request worker that reuses its connection, `SQLiteSession` owns the native
handle and closes it through `using`. Keep its provider and each generated
context within that worker; operations on one connection are sequential.
`SQLiteOptions` controls journal mode, synchronization and a bounded lock wait.
The default preserves the database journal, selects Full synchronization and
returns immediately on lock contention. SQLite still has one active writer.

`ProviderFailure.Driver` preserves the provider, operation, error category,
message and native codes. `ProviderFailure.State` describes adapter misuse;
cleanup failures preserve both causes. A failed transaction is never replayed
automatically. Streaming deadlines bound cooperative execution and lock waits,
but cannot preempt an operating-system call. Resource close reports
`result::Failure` with native details in `nativeError`.

Writable WAL connections require SQLite 3.51.3 or newer, or the patched 3.44
(3.44.6+) and 3.50 (3.50.7+) branches. This applies both to requesting WAL and
preserving an existing WAL database. Use a patched SQLite build to convert an
existing WAL database to DELETE; opening it for that conversion also requires
the fix. DELETE databases retain the 3.37.2 minimum. See the
[SQLite WAL-reset fix](https://sqlite.org/wal.html#walreset_bug).

The default journal policy preserves the existing mode and uses `Full`
synchronization; durability still depends on the journal mode and filesystem.
With DELETE, `Full` can lose the last commit after a power failure; `Extra` also
synchronizes the directory after journal deletion. WAL with `Full` or `Extra`
synchronizes commits, while WAL with `Normal` can lose commits after an OS or
power failure. Process-termination recovery tests do not simulate power loss.

Keep the database and its backup files outside build output. Use SQLite's online
backup mechanism for a live database. Normalize the offline backup destination
to DELETE journal mode before closing it so the snapshot can be reopened without
WAL sidecars. Before restoring, require `PRAGMA integrity_check` to return exactly
one `ok` row on the snapshot. Restore
to a fresh path with all application connections closed, then require the same
integrity result on the restored database. Also run
`PRAGMA foreign_key_check` when the schema has foreign keys, because integrity
checking does not detect their violations. Verify application records and write
reuse before promoting the restored database. The recovery contract exercises
this restore path with both DELETE and WAL journals after an interrupted writer.
Copying only the main file of a live WAL database can omit committed changes.
See SQLite's
[online backup API](https://sqlite.org/backup.html),
[integrity checks](https://sqlite.org/pragma.html#pragma_integrity_check),
[read-only WAL requirements](https://sqlite.org/wal.html#read_only_databases), and
[synchronization settings](https://sqlite.org/pragma.html#pragma_synchronous).

This separation follows [EF Core's context and connection lifetime](https://learn.microsoft.com/en-us/ef/core/dbcontext-configuration/)
and [query materialization](https://learn.microsoft.com/en-us/ef/core/querying/).
[Rails relation inspection](https://api.rubyonrails.org/v8.1.0/classes/ActiveRecord/Relation.html#method-i-inspect)
also distinguishes the visible preview from a complete result.

Generated APIs, mappings and native providers are validated with the matching
compiler/runtime release. Internal declarations and generated private layout are
implementation details; application code uses the generated context and public
library operations. The [runtime ABI](ABI.md) separately governs native binary
compatibility.
