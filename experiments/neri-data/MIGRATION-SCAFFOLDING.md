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
application library that references the Neri Data runtime, then explicitly
assemble its migration catalog in the intended order. Historical factories
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

Automatic catalog discovery, removing the last migration, pending-model checks,
SQL script generation, database inspection and reverse engineering remain
outside this scaffolding contract.

## Verification

```sh
scripts/neri.sh run --project tooling/data --unit migration-contract -- "$PWD"
scripts/neri.sh run --project experiments/neri-data/migration-scaffold --unit contract
scripts/neri.sh run --project experiments/neri-data/migration-scaffold --unit contract --release
scripts/neri.sh run --project . --unit data-generation-contracts -- "$PWD"
```

The tooling contract checks snapshot round trips, malformed metadata, unchanged
models, preservation of existing destinations, editable historical source and
changed-snapshot rejection. The native consumer reconstructs every supported
operation from generated source and compares signatures, including integer
extrema, escaped text and logical identities containing NUL. Two CLI-generated
bundles exercise upgrading existing SQLite rows, physical column renaming,
default/index changes and both downgrade steps.

To regenerate the operation matrix, pass the absolute path of
`experiments/neri-data/migration-scaffold/all_ops/migration.hk` as the tooling
contract's second argument. CLI-generated bundles require fresh output
directories because existing migrations are preserved.

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
