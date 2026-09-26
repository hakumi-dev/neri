# Schema migrations and inspection

Neri Data represents schema changes as typed `SchemaOp` values. A `Migration`
contains a stable identifier and explicit ordered `up` and `down` operations;
`MigrationSet` preserves catalog order. SQLite executes a catalog through
`SQLiteProvider.migrate(catalog, target)`:

- Omitted or null target applies through the last catalog entry.
- A migration identifier selects the schema after that migration.
- An empty target returns to the baseline by executing down operations in
  reverse migration order. Each migration's down operations retain their
  declared order.

```neri
let initial = new Migration("initial", [
  SchemaOp.CreateTable("people", [
    new SchemaColumn("id", SchemaType.Int(), primaryKey: true),
    new SchemaColumn("name", SchemaType.Text())
  ])
], [SchemaOp.DropTable("people")])
let catalog = new MigrationSet([initial])

match SQLiteProvider.createWritable("app.sqlite")
  case SQLiteOpenResult.Opened(provider)
    let migration = provider.migrate(catalog)
    # Inspect the result before using the application context.
    let closed = provider.close()
  case SQLiteOpenResult.Failure(message)
    console::println(message)
end
```

`createWritable` creates a missing database or opens an existing one.
`openWritable` continues to require an existing file. Both enable foreign-key
enforcement on their connection. Migrating requires SQLite 3.35 or later, a
writable open provider, and no active context transaction.

## Operations and history

The compiler supports create/drop table, add/drop column, rename table/column,
and create/drop index. Columns support Bool, Int, Float and Text, nullable values,
and typed constant defaults. `SchemaDefault.Null()` explicitly declares SQL
`DEFAULT NULL` for nullable columns; a null `defaultValue` means no declaration.
These forms have different migration signatures. Float columns use SQLite REAL and accept only
finite Float defaults; see [FLOATS.md](FLOATS.md). A created table has exactly one required Int
or Text primary key. Text keys declare `NOT NULL` explicitly. A required added
column needs a default. Identifiers and default
types are validated; text defaults are quoted as literals, including embedded
quotes. Names beginning with `__neri_` are reserved for runtime metadata.
The compiler does not accept an arbitrary SQL operation.

`SchemaOp.RebuildTable(before, after)` carries copied table snapshots for
non-key type, nullability and default changes, including related physical
renames and index changes. It requires provider execution instead of a single
compiled statement. See [typed table rebuilds](TABLE-REBUILDS.md) for live-schema
validation, conversion semantics and transactional recovery.

Migration definitions snapshot their input arrays. Their signature is the
complete, versioned, length-delimited canonical representation of both
directions. It is compared exactly, rather than reduced to a collision-prone
checksum. Signatures identify migration contents; they are not cryptographic
proof of database authenticity.

Each migration requires at least one operation in each direction. The SQLite
runner accepts catalogs of up to 1000 entries. Migration identifiers follow
the same identifier grammar as mapped names and must be unique without regard
to ASCII letter case; target lookup and persisted identifiers match exactly.

The applied history must be an exact prefix of the supplied catalog, including
identifiers, order, and signatures. Modified, missing, reordered, or unknown
applied migrations fail validation before application DDL. Appending migrations
preserves the existing prefix. Reapplying the same target returns zero changes.
The result's count is the number of migrations executed, not rows or statements.

SQLite acquires `BEGIN IMMEDIATE` before reading history. The complete requested
upgrade or downgrade and its history changes commit together; failures roll
back that transaction. Other writers cannot race the history check while the
lock is held. Lock acquisition can fail and the caller may retry. Migration
execution leaves foreign-key enforcement enabled, so schema operations retain
SQLite's constraint and cascade behavior.

Down operations are authored explicitly. Dropping a table or column can lose
data; applying an inverse schema operation does not reconstruct its contents.
Transaction rollback on failure preserves the pre-call state. Successful
downgrade and failure rollback are different operations.

## Inspecting the database

`SQLiteProvider.inspectSchema()` returns a readonly view of column metadata in
the main database: table/column names, declared type, default SQL expression,
declared NOT NULL flag, primary-key position, generated/hidden-column flag, and
table creation SQL. It uses one statement joining `sqlite_schema` with
`pragma_table_xinfo`, ordered by table name and declared column position.
SQLite internal tables and the reserved Neri namespace are excluded.

Inspection works on read-only connections. A snapshot contains at most 1000
columns; larger schemas return a failure instead of a truncated snapshot.
The NOT NULL flag describes the declaration, rather than inferring SQLite's
special primary-key rules. Default expressions and creation SQL are metadata,
not executable migration plans.

The [model migration planner](MODEL-MIGRATIONS.md) compares typed desired-state
snapshots and produces operations for this runner. Inspection does not infer
those models from arbitrary database DDL. [Migration-file scaffolding](MIGRATION-SCAFFOLDING.md)
uses authored model snapshots. Automatic entity scaffolding and a complete
index/foreign-key catalog are outside schema inspection. Inspection does not establish that
an application's migration history matches all external DDL changes.

## Verification

Contracts cover whole-call upgrade rollback, partial downgrade rollback,
modified signatures and malformed history ordinals, foreign-key-protected
drops, and a deferred constraint that fails specifically at COMMIT. They verify
restored rows, schema and history before retry. A generated application context
also saves and queries entities in a database created by the migration API.

```sh
scripts/neri.sh run --project experiments/neri-data --unit migration-contract
scripts/neri.sh run --project experiments/neri-data/providers/sqlite --unit migration-contract
scripts/neri.sh run --project experiments/neri-data/providers/sqlite --unit migration-contract --release
scripts/neri.sh run --project experiments/neri-data/providers/sqlite --unit schema-inspection-contract
scripts/neri.sh run --project experiments/neri-data/providers/sqlite --unit schema-inspection-contract --release
scripts/neri.sh run --project experiments/neri-data/provider-contract --unit migrations --release
```

## Verified primary references

- Curino, Moon and Zaniolo, [Graceful Database Schema Evolution: the PRISM
  Workbench](https://www.vldb.org/pvldb/vol1/1453939.pdf), PVLDB 1(1), 2008,
  pp. 761–772, §§1 and 4. Schema modification operators and explicit evolution
  history motivate the typed operation catalog. Neri does not implement PRISM's
  query rewriting or invertibility analysis. Exact prefix/signature checks and
  the whole-call transaction are Neri engineering choices.
- Microsoft, [EF Core migrations overview](https://learn.microsoft.com/en-us/ef/core/managing-schemas/migrations/):
  applied migration history and source-controlled changes provide the comparison
  contract. Neri supports typed model snapshots and operation planning as described
  in [Model migrations](MODEL-MIGRATIONS.md).
- SQLite, [transactions](https://sqlite.org/lang_transaction.html),
  [foreign keys](https://sqlite.org/foreignkeys.html), and
  [ALTER TABLE](https://sqlite.org/lang_altertable.html): write locking,
  commit/rollback behavior, foreign-key effects of DROP, and native alteration
  limits determine execution behavior.
- SQLite, [PRAGMA functions and table_xinfo](https://sqlite.org/pragma.html):
  table-valued metadata functions allow a single query to include ordinary,
  hidden, and generated columns.
- SQLite, [3.35 release notes](https://sqlite.org/releaselog/3_35_0.html):
  DROP COLUMN establishes the minimum migration engine version.
