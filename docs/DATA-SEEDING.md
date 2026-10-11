# Model and migration seed data

`SchemaTable(identity, name, fields, indexes, seeds)` accepts immutable `SchemaSeed`
rows. A seed supplies complete assigned primary-key fields and values, followed by
non-key fields and values. Field names are stable logical model identities; the
migration compiler resolves their current physical column names. Constructors copy
arrays and binary values. Canonical ordering makes signatures independent of row or
field input order. Duplicate keys, unknown fields, mismatched types, null keys and
missing required values are rejected before any migration executes.

`diffSchema` produces parameterized `InsertData`, `UpdateData` and `DeleteData`
operations in both directions. It deletes removed rows before schema changes and
applies inserted/updated rows after them. Seed tables follow foreign-key dependency
order; deletions reverse that order. Cyclic dependencies are accepted when the
outstanding foreign keys are declared deferred. `SchemaReference(..., deferred:
true)` emits `DEFERRABLE INITIALLY DEFERRED` on both providers.

When retained dependent seeds change their foreign keys alongside principal removal,
the diff inserts replacement principals and updates dependent values against the
source schema before deleting old keys. It then changes the schema and applies
remaining target fields. Stable logical fields preserve this ordering across column
renames and added fields. Values needed by the source schema must be available from
the target seed, an existing managed seed, or a source default. Removing a managed field while its column remains requires
an explicit replacement value. A model seed owns its specified values; applications
should use ordinary tracked writes for mutable operational data.

`SchemaOp.InsertData(table, columns, values)`, `UpdateData(table, keyColumns,
keyValues, columns, values)` and `DeleteData(table, keyColumns, keyValues)` can also
be authored directly. These operations participate in catalog signatures, guarded
history and the same transaction as surrounding DDL. SQLite and PostgreSQL preserve
catalog operation order in synchronous and async application. Each seed command must
affect exactly one row; a missing key is a migration failure and rolls back the
migration. Applying an already completed catalog does not repeat its seed operations.
Generated scripts render escaped literals; provider execution binds the values.
