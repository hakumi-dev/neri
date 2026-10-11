# Migration source scaffolding

The migration scaffolder compares a validated mapping with a historical model
snapshot and writes a new, reviewable migration bundle. Planning and source
generation do not open a database or execute schema operations.

```sh
scripts/neri.sh run --project tooling/data --unit migration-scaffold -- path/to/mapping.json empty initial migrations/initial
scripts/neri.sh run --project tooling/data --unit migration-scaffold -- path/to/mapping.json migrations/initial/snapshot.json evolved migrations/evolved
```

The four arguments are the mapping file, a previous `snapshot.json` or the
literal `empty`, a migration identifier, and a new output directory. Relative
historical snapshot paths and output directories resolve from the mapping
directory; absolute paths are supported. The output parent must already exist. An unchanged model creates
no bundle. Invalid metadata and unsupported changes fail before publication.

Each completed bundle contains:

- `migration.hk`: literal `Migration` operations for both directions and a
  literal `SchemaModel` factory for the resulting schema.
- `snapshot.json`: the complete historical schema, with a format version and
  the model's signature.
- `scaffold.json`: completion metadata and artifact digests, written last.

Source factories are named `migration_<id>()` and `snapshot_<id>()` in the
mapping namespace's `migrations` namespace. Include the source file in an
application library that references the Neri Data runtime, or publish a catalog
with the managed commands below. Historical factories
contain scalar schema definitions and do not depend on the current application
entity classes or the regenerated current-model factory.

```neri
let catalog = new MigrationSet([migration_initial(), migration_evolved()])
let result = provider.migrate(catalog)
```

## Review and preservation

The source includes the planner's data-loss flags and signatures. Review the
operations and both directions before applying a migration. Stable logical
identities determine renames; changing an identity is an addition/removal.
A generated downgrade restores supported schema structure, not deleted data.
The scaffolder retains the existing planner's limits and does not infer data
transformation code or resolve unsupported primary-key changes.

Existing output directories are rejected. Publication exclusively reserves the
new directory, writes and verifies its artifacts, checks the mapping inputs,
and writes the completion marker last. A process interruption may leave an
incomplete directory; it is not a usable historical bundle and is never
automatically overwritten. Publication is not a multi-file filesystem
transaction or a database transaction.

Migration source is reviewable application code. Editing it does not change
the historical snapshot; a later scaffolding operation checks that snapshot's
integrity rather than executing the earlier migration. Keep snapshots consistent
with any authored schema-operation changes. Completion metadata records the
source digest at generation time, not an approval or a permanent source lock.

Snapshots distinguish an absent default from `DEFAULT NULL`, preserve index
key order, and represent integer and floating-point defaults as strings to
avoid JSON number conversion. Reading validates types, metadata, format version
and the recomputed model signature. Snapshot text is limited to 4 MiB. These
signatures describe metadata; they are not evidence of the live database schema.

## Managed catalogs

A catalog directory contains completed bundles and generated `catalog.hk` and
`manifest.json` files. Discovery verifies the source, snapshot and marker
digests, then follows before/after signatures from the empty model. Directory
and identifier spelling do not determine order. Missing bundles, branching
history, edited generated operations, unknown files and symlinks fail discovery.
The runtime project reference is resolved from the mapped data unit.

```sh
scripts/neri.sh run --project tooling/data --unit migration-scaffold -- discover mapping.json migrations
scripts/neri.sh run --project tooling/data --unit migration-scaffold -- check mapping.json migrations
scripts/neri.sh run --project tooling/data --unit migration-scaffold -- add mapping.json migrations add_address
scripts/neri.sh run --project tooling/data --unit migration-scaffold -- script mapping.json migrations empty add_address sqlite initial.sql
scripts/neri.sh run --project tooling/data --unit migration-scaffold -- remove-last mapping.json migrations add_address sqlite application.sqlite
```

The catalog root must already exist. `check` exits with 0 when the current model
matches its last snapshot, 3 for pending changes, and 1 for invalid inputs.
`add` discovers the historical model automatically, reserves the catalog lock,
publishes one bundle and updates the generated catalog. An unchanged model
creates no migration. Reference the generated catalog's library unit and call
its `migrations()` factory to apply the actual compiled operations.

`remove-last` requires the expected final identifier and a live database.
It supports `sqlite` with an existing writable database path and `postgres`
with PostgreSQL connection information accepted by the adapter. It acquires a
provider transaction and the migration lock, validates the database's full
applied prefix, and rejects an applied final migration. The directory is staged
inside `.neri-migrations.lock` before catalog publication; publication failure
restores the directory. Cleanup failure preserves remaining staged files and
the lock for explicit recovery. The command does not downgrade a database.

`script` accepts exact starting/ending identifiers or `empty`, and a
`sqlite` or `postgres` dialect. It writes a new SQL file, verifies the starting
history prefix inside the script, emits the selected up/down operations, and
updates migration stamps transactionally. PostgreSQL scripts share the provider's
advisory migration lock. SQLite scripts disable foreign keys before beginning
and check them before committing. Execute scripts with an SQL runner that stops
on the first error, rolls back an open transaction, and restores SQLite foreign
keys after failure. Scripts contain DDL and history checks; the SQLite provider
also performs live schema and strict copied-value validation during rebuilds.
Scripts do not reproduce that provider inspection.

Catalog artifacts are generated ownership boundaries. Authored edits require
consistent snapshots, operations and digests before managed discovery; the
legacy four-argument scaffolder still accepts a validated historical snapshot
independently of edits to its source. General database reverse engineering
remains outside this workflow.

## References

Primary references verified on 2026-09-23:

- Microsoft, [Managing migrations](https://learn.microsoft.com/en-us/ef/core/managing-schemas/migrations/managing):
  generated up/down operations and historical snapshots support subsequent
  model comparisons; migrations remain reviewable source-controlled code.
- Microsoft, [EF Core CLI](https://learn.microsoft.com/en-us/ef/core/cli/dotnet):
  migration scaffolding is a distinct design-time operation. Neri's command
  currently produces explicit bundles and does not implement the full CLI.
- Curino, Moon and Zaniolo, [Graceful Database Schema Evolution: the PRISM
  Workbench](https://www.vldb.org/pvldb/vol1/1453939.pdf), PVLDB 1(1), 2008,
  pp. 761–772: explicit schema modification operators and recorded evolution
  motivate inspectable historical plans. Neri does not implement PRISM's query
  rewriting or invertibility analysis.
