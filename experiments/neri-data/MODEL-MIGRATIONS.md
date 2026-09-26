# Model snapshots and migration planning

`SchemaModel` describes desired tables, scalar columns and indexes. Each
`SchemaTable` and `SchemaField` has a stable logical identity separate from its
physical SQL name. `SchemaIndex` lists logical field identities in index-key
order. Models copy their inputs and canonicalize table/field identities and
index names; reordering those input arrays does not change their signature.
Index-key order remains significant. Signatures are versioned, length-delimited
representations, not cryptographic hashes.

The data generator emits `schema_<Context>()` in an owned `schema.hk` artifact.
It describes the mapped scalar types, nullability, primary key, constant defaults
and named indexes. Mapping v1 declares defaults on column entries and indexes on
entities; foreign keys and computed columns remain outside this metadata.
Explicit `SchemaModel` values can also declare typed constant defaults and
indexes. Historical snapshots should
be kept as independent, source-controlled definitions; regenerating the current
model does not update the previous snapshot used to plan a migration.

```neri
let before = new SchemaModel([])
let after = schema_Database()

match diffSchema("initial", before, after)
  case SchemaDiffResult.Unchanged()
    console::println("No model changes")
  case SchemaDiffResult.Failure(error)
    # Handle invalid metadata or an unsupported change.
  case SchemaDiffResult.Plan(plan)
    let migration = plan.migration()
    # Inspect both directions and plan.upDataLoss()/plan.downDataLoss().
    let catalog = new MigrationSet([migration])
    # Apply explicitly with provider.migrate(catalog).
end
```

## Declarative defaults and indexes

Column entries accept an optional `default` property. Its JSON value must match
the mapped scalar: boolean for Bool, signed 64-bit integer without a fraction
or exponent for Int, finite binary64 number for Float, and text for String.
Float exponent notation is accepted and normalized to a Neri source literal;
overflow and nonzero numbers that underflow to zero are rejected. Text preserves
Unicode, quotes, backslashes and control characters except NUL, which is rejected.
Primary keys cannot declare defaults. JSON `null` is allowed only on optional
fields and emits `SchemaDefault.Null()` / SQL `DEFAULT NULL`; omitting the property
means no default. Those two declarations have different snapshot signatures.

Each entity can declare an `indexes` array:

```json
{
  "name": "customers_name_active",
  "members": ["name", "active"],
  "unique": true
}
```

`members` contains mapped Neri field names, including inherited fields, in
index-key order. Generated metadata resolves them to canonical field identities.
`unique` defaults to false. Empty or repeated members, unknown fields, malformed
declarations, invalid SQL identifiers and the reserved `__neri_` prefix fail at
mapping load. Index names must be unique across the model and cannot collide
with table names, ignoring ASCII case. Table and column names likewise reject
case-insensitive duplicates within their respective SQL scopes.

These declarations configure the desired database schema. A SQL default applies
when an INSERT omits the column. A `default` alone retains ordinary scalar writes,
including explicit false, zero and null. Opting into
[`valueGeneration`](GENERATED-PROPERTIES.md) separately omits the column and
refreshes its returned value in the tracked entity.
SQL expression defaults, partial/descending indexes and included columns remain
outside this contract.

## Changes and ordering

`diffSchema(id, before, after)` returns a validated `Migration` through
`SchemaMigrationPlan`, or a failure without a partial plan. It does not connect
to a database or apply DDL. The plan exposes both snapshot signatures and flags
for potential data loss in each direction. The existing migration catalog,
history signatures and whole-call SQLite transaction handle execution.

Supported differences include creating/dropping tables, adding/removing nullable
or defaulted non-key columns, renaming physical tables/columns while retaining
logical identity, and creating/removing/replacing indexes. Changes to existing
non-key types, nullability and defaults use a typed table-rebuild operation;
see [table rebuilds](TABLE-REBUILDS.md) for conversion and live-schema checks. Required column
additions need typed defaults, including additions in the generated downgrade.
Single required Int and Text primary keys are supported; Text keys explicitly
declare `NOT NULL` because SQLite does not imply it for ordinary text primary
keys.

Each direction drops affected indexes before their columns or tables, performs
table/column changes, and creates indexes after their fields exist. Unchanged
indexes survive table/column renames through SQLite's rename behavior. Both
directions pass the same schema-operation and migration-catalog validation as
authored migrations. A rebuilt table owns its complete column and index changes
in one operation, including any physical rename.

A physical name change with the same logical identity is an explicit rename.
Changing a logical identity is an addition/removal; names are never matched by
similarity. Reusing another identity's physical table/column name, name swaps,
and case-only table/column renames are rejected. Models reject case-insensitive
physical-name collisions and indexes that reference unknown fields.

Dropping a table or column sets the corresponding data-loss flag. Recreating
the schema on downgrade does not recover deleted values. Adding a field or
table therefore ordinarily marks the downgrade as potentially losing data.
Type conversions also set the data-loss flag because conversion may alter the
stored representation. Primary-key changes remain unsupported. Tightening
nullability requires compatible live values; defaults do not replace existing
NULL cells during a rebuild.
Planning is bounded to 1000 tables, fields and indexes combined per snapshot.

## Boundaries

Model comparison is independent of database inspection. It does not certify
that a live database matches a model or detect externally created indexes,
triggers, views or foreign keys. Table rebuild execution separately checks the
affected live table and its dependencies before replacing it. Other SQLite DDL
may reject such objects; the migration transaction preserves the pre-call
database on failure.

[Migration scaffolding](MIGRATION-SCAFFOLDING.md) persists typed operations and
independent historical snapshots as reviewable bundles. Compound/alternate key
constraints, database foreign-key modeling and reverse engineering remain
unsupported. Callers assemble and apply their migration catalog explicitly.

## Verification and references

```sh
scripts/neri.sh run --project experiments/neri-data/providers/sqlite --unit schema-diff-contract
scripts/neri.sh run --project experiments/neri-data/providers/sqlite --unit schema-diff-contract --release
scripts/neri.sh run --project . --unit data-generation-contracts -- "$PWD"
scripts/neri.sh run --project . --unit data-schema-mapping-contracts -- "$PWD"
scripts/neri.sh run --project experiments/neri-data/provider-contract --unit migrations
scripts/neri.sh run --project experiments/neri-data/provider-contract --unit migrations --release
```

Primary references verified on 2026-09-23:

- Microsoft, [Generated values](https://learn.microsoft.com/en-us/ef/core/modeling/generated-properties#default-values):
  defaults apply to omitted insert values; configuring defaults and retrieving
  generated property values are distinct responsibilities.
- Microsoft, [Indexes](https://learn.microsoft.com/en-us/ef/core/modeling/indexes):
  named, unique and composite indexes motivate the mapping surface; index-key
  order matters. EF's richer index features are outside this model contract.
- SQLite, [DEFAULT clause](https://www.sqlite.org/lang_createtable.html#the_default_clause):
  defaults are used for omitted insert columns; Neri emits typed constant SQL
  literals and distinguishes explicit NULL from absent metadata.
- Microsoft, [Managing migrations](https://learn.microsoft.com/en-us/ef/core/managing-schemas/migrations/managing):
  model snapshots support comparison with later models; generated operations
  need review because renames and destructive changes can be ambiguous. Neri's
  stable identity rule and typed planner are its own implementation contract.
- Curino, Moon and Zaniolo, [Graceful Database Schema Evolution: the PRISM
  Workbench](https://www.vldb.org/pvldb/vol1/1453939.pdf), PVLDB 1(1), 2008,
  pp. 761–772: explicit schema modification operators and documented evolution
  motivate inspectable plans. Neri does not implement PRISM's query rewriting
  or mapping invertibility analysis.
- SQLite, [ALTER TABLE](https://www.sqlite.org/lang_altertable.html): native
  operations have version-specific constraints; the generalized rebuild sequence
  creates a replacement, copies rows, drops the original and renames the
  replacement. Neri retains its SQLite 3.35 minimum and uses rebuilds for
  non-key type, nullability and default changes.
- SQLite, [PRIMARY KEY semantics](https://www.sqlite.org/lang_createtable.html#the_primary_key):
  ordinary non-integer primary keys require an explicit `NOT NULL` constraint
  to reject null keys.
