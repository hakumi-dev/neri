# Database schema scaffolding

`SQLiteProvider.scaffoldSchema(options)` and
`PostgreSQLProvider.scaffoldSchema(schema, options)` return
`SchemaScaffoldResult.Source(modelSource, mappingJson)` or a structured provider
failure. `SchemaScaffoldOptions` controls the model/data namespace, context,
project manifest, entity unit and generated output directory. The caller saves
the two artifacts, adds the model source to its entity unit, and runs the
ordinary data generator with the mapping.

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
PostgreSQL accepts `bigint`, `boolean`, `double precision`, `text` and `bytea`.
Other types and type modifiers fail instead of applying an affinity or losing
precision. Unsupported generated key combinations and nullable primary keys
also fail. SQLite rowid aliases are distinguished from assigned keys in
WITHOUT ROWID tables and indexed primary keys.

Physical identifiers must fit the mapping's identifier contract. Neri class,
set and field names have deterministic prefixes and byte escapes, with collision
checks. Inspection and scaffolding are bounded to 1000 visible columns, foreign-key
components and indexes.

Authored mappings accept column `defaultSql`, `computedSql`, `computedStored`,
`checkSql` and `collation` facets. `SchemaColumn` carries the same facets through
migration signatures, snapshots, scripts and provider execution. SQL expressions
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

Custom SQL uses the provider's transactional capabilities. PostgreSQL concurrent
index creation and other operations requiring execution outside a transaction
remain unsupported by the atomic migration contract. PostgreSQL index methods,
operator classes, null-distinct policies and storage options can be authored as
custom SQL; reverse engineering accepts the index facets represented by the typed
mapping and rejects unsupported index definitions.
