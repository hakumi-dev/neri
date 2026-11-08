# Database schema scaffolding

`SQLiteProvider.scaffoldSchema(options)` and
`PostgreSQLProvider.scaffoldSchema(schema, options)` return
`SchemaScaffoldResult.Source(modelSource, mappingJson)` or a structured provider
failure. `SchemaScaffoldOptions` controls the model/data namespace, context,
project manifest, entity unit and generated output directory. The caller saves
the two artifacts, adds the model source to its entity unit, and runs the
ordinary data generator with the mapping.

Set `SchemaScaffoldOptions.tables` to physical table and view names to scaffold
a selection; its default empty array selects all visible relations. Names match
exactly and use ASCII identifier names of at most 63 bytes. Both entry points
reject duplicate, invalid or oversized selections. Scaffolding also rejects
missing names; inspection returns metadata for the matching visible relations.
Both providers apply selection before
reading column, foreign-key and index metadata, so unsupported definitions in
unselected relations do not prevent scaffolding. A foreign key is included when
both its dependent and principal tables are selected in the inspected schema.
Cross-schema foreign keys are omitted because mapping uses unqualified table names.
Physical column and table
ordering remains deterministic. The same options can select `inspectSchema`
metadata without generating source.

The generated classes preserve physical column order, nullable types and
ordered primary-key members. A single generated integer key uses `onAdd`;
defaulted non-key fields use `onAdd`; computed fields use `onAddOrUpdate`.
Views and tables without primary keys use keyless query sources. Database
defaults and computed expressions are retained as escaped source comments and
mapping `defaultSql`/`computedSql` facets. The mapping also carries stored/virtual
generation, column checks and collations.

Foreign keys preserve ordered dependent/principal tuples, database delete actions
and initially deferred constraints. Indexes preserve unique keys, descending terms,
expression SQL and partial predicates; PostgreSQL also preserves included columns.
SQLite unnamed unique constraints receive deterministic scaffold index names.
Triggers and provider-specific objects beyond this mapping remain database-owned.
Owned inspection sessions use a read transaction so their metadata statements share
a consistent snapshot. SQLite
inspection includes main-database tables and views and requires SQLite 3.37.2
or newer, matching the provider minimum. PostgreSQL scaffolding accepts the
`public` schema; other schemas require schema-qualified mapping support.

SQLite accepts the exact declared types `INTEGER`, `REAL`, `TEXT` and `BLOB`.
PostgreSQL accepts `bigint`, `boolean`, `double precision`, `text` and `bytea`,
plus `smallint`, `integer`, `real` and `character varying` with an optional length.
Scaffolding maps these additional types to `Int`, `Float` and `String` and emits
the `postgresqlType` facet so migration DDL recreates the original storage type.
The type retains PostgreSQL's range, precision and length rules.
[PostgreSQL 14 numeric types](https://www.postgresql.org/docs/14/datatype-numeric.html)
and [character types](https://www.postgresql.org/docs/14/datatype-character.html).
For the additional integer types, scaffolding accepts assigned keys and rejects
identity columns and SQL-defaulted keys whose generation definitions require
authored schema.
Native PostgreSQL scalar facets map to the following model types.

| PostgreSQL type | Neri model type | Exact domain |
| --- | --- | --- |
| `numeric`, `numeric(p,s)` | `Decimal` | Finite values within the Decimal digit bound and declared precision and scale. |
| `date` | `DateOnly` | Gregorian dates from year 0001 through 9999. |
| `time without time zone` | `TimeOnly` | Clock times before 24:00 at the declared precision. |
| `timestamp without time zone` | `DateTime` | The stored wall-clock value is interpreted in UTC. |
| `timestamp with time zone` | `DateTime` | UTC instants within the DateTime range, normalized from the server offset. |
| `interval` | `TimeSpan` | Exact signed durations with zero calendar months and days. |

Time, timestamp and full-range interval facets accept fractional precision from
0 through 6. Typed parameters that would require rounding fail before dispatch.
Date and timestamp results use PostgreSQL ISO date output. Interval results use
the `postgres` interval style. Nonfinite values, calendar intervals,
`time with time zone` and offset-preserving native `DateTimeOffset` storage are
outside the native scalar contract. Portable codecs retain their existing
storage and representation. [PostgreSQL 14 date and time types](https://www.postgresql.org/docs/14/datatype-datetime.html)
and [libpq parameters](https://www.postgresql.org/docs/14/libpq-exec.html).

Native scalar facets apply to non-key fields with their built-in model codec.
Other types and modifiers fail inspection-to-source conversion. Unsupported
generated key combinations and nullable primary keys also fail. SQLite rowid aliases are
distinguished from assigned keys in WITHOUT ROWID tables and indexed primary keys.

Physical identifiers must fit the mapping's identifier contract. Neri class,
set and field names have deterministic prefixes and byte escapes, with collision
checks. Inspection and scaffolding are bounded to 1000 visible columns, foreign-key
components and indexes.

Authored mappings accept column `defaultSql`, `computedSql`, `computedStored`,
`checkSql` and `collation` facets. `SchemaColumn` carries the same facets through
migration signatures, snapshots, scripts and provider execution. The optional
`postgresqlType` facet accepts the additional PostgreSQL types listed above and
requires a matching model storage codec. SQLite rejects DDL using this facet.
Changing the facet of an existing column requires explicit migration SQL to
choose how existing values convert. An explicit cast to a shorter `character
varying` can truncate existing values. [PostgreSQL 14 ALTER TABLE](https://www.postgresql.org/docs/14/sql-altertable.html)
and [character cast rules](https://www.postgresql.org/docs/14/datatype-character.html).
SQL expressions
must be bounded and balanced, with no statement separators or comments outside
quoted literals. Index mapping facets are `descending`, `expressions`, `include`
and `predicate`. Member and include lists use model members; expressions and
predicates use trusted physical SQL. `SchemaOp.CreateIndex` takes a fifth
`SchemaIndex?` facet payload; use null for an ordinary column index.

`SchemaOp.Sql(statement)` adds trusted, application-authored transactional DDL or
DML to an explicit migration's up/down catalog. It supports data transformations,
triggers and PostgreSQL policies, extensions and functions that the typed schema
model does not describe. SQL operations preserve their authored position between
ordinary schema operations, participate in the catalog signature, and execute in
the same transaction in synchronous and asynchronous migrations.

Each SQL operation contains one statement of at most 65536 UTF-8 bytes. The
validator rejects transaction control, statement batches and unbound parameters.
SQLite trigger bodies may contain their required `BEGIN`/`END` statement list.
PostgreSQL accepts dollar-quoted bodies and executes custom operations with the
extended protocol's single-statement check. Single-quoted literals use doubled
quotes; backslash escapes are rejected. PostgreSQL dollar quoting can carry
backslashes inside function bodies. SQL text comes from application source;
runtime data uses the parameterized seed operations.

Scaffolded migrations and guarded SQL scripts use atomic ranges. Author operations
that require autocommit, such as PostgreSQL concurrent index creation, as
[isolated runtime migrations](../packages/data/docs/MIGRATION-RECOVERY.md).
PostgreSQL index methods,
operator classes, null-distinct policies and storage options can be authored as
custom SQL; reverse engineering accepts the index facets represented by the typed
mapping and rejects unsupported index definitions.
