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
defaults and computed expressions are retained as escaped source comments;
the database continues to own their SQL definitions. The mapping captures
runtime query/write policy and does not reconstruct those definitions in a
new database.

Scaffolding is partial reverse engineering. Foreign keys, indexes, checks,
triggers, collations and other database facets remain in the inspected database.
Generated model/mapping artifacts target that existing database. SQLite
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
checks. Inspection and scaffolding are bounded to 1000 visible columns.
