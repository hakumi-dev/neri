# Typed table rebuilds

`diffSchema` plans `SchemaOp.RebuildTable(before, after)` when an existing
non-key field changes type, nullability or default. Both arguments are copied
`SchemaTable` snapshots with stable logical identities. The operation includes
physical table/column renames, added/removed fields and the target indexes.
Primary-key identity and scalar definition must remain stable; its physical
column name can change. New required columns
need constant defaults. Both migration directions are validated independently.

The operation is executed by the SQLite migration provider inside its existing
whole-call transaction. `compileSchema` reports that a rebuild requires provider
execution rather than returning a standalone SQL statement. Migration signatures
include both table definitions, while existing operation signatures retain their
encoding. Altering an applied rebuild therefore fails history-prefix validation.

## Data and failure behavior

The provider checks the live source, creates a replacement table, copies columns
by logical field identity, validates the copied values, drops the source, renames
the replacement and creates the target indexes. It never renames the source to
the temporary name before copying. Temporary-name collisions fail without
replacing the existing object.

Shared fields copy their current cells, even when the target default changes.
New fields are omitted from the copy so their declared defaults or NULL apply.
Defaults affect later INSERTs that omit the column; they do not fill existing
NULL values when a column becomes required.

Type changes use SQLite destination affinity during `INSERT ... SELECT`, without
an explicit lossy `CAST`. Copied cells must satisfy the target mapped scalar
domain before the source is dropped: requiredness, integer storage, Boolean
0/1, finite Float and valid text. Nonnumeric text cannot silently become integer
zero. Conversion may still alter representation, so plans flag type changes as
potential data loss in both directions. This is not a proof of lossless numeric
or textual conversion; applications must review the selected type change.

Failed copying, typed validation, index creation or commit rolls back schema,
rows and migration history together. A caller can correct incompatible source
values and retry the same catalog. A downgrade restores the prior definition;
it cannot recover deliberately dropped values or guarantee reversal of a type
conversion.

## Live-schema boundaries

Before replacing a table, the provider compares its declared columns and indexes
with the source snapshot. Unmodeled columns, generated columns, expression or
partial indexes, custom collations and unsupported table constraints/options
cause an explicit failure. Foreign keys or triggers/views depending on the
table must be modeled by a future richer schema contract or handled separately;
rebuild does not silently discard them. Foreign-key enforcement stays enabled.

The supported schema is the current scalar model with a single Int or required
Text primary key and named ordinary indexes. Implicit SQLite rowids are not
modeled application keys. The preflight is scoped to rebuild execution; it does
not turn model comparison into general live database drift certification.
It uses the existing bounded schema inspector. Dependency SQL matching is
conservative and can reject a trigger or view containing the table's name even
when that occurrence is not a dependency. Plans involving an index-name transfer
between tables and a rebuild are rejected before execution; coordinated index
transfers are unsupported.

## Verification and references

```sh
scripts/neri.sh run --project experiments/neri-data --unit migration-contract
scripts/neri.sh run --project experiments/neri-data/providers/sqlite --unit schema-rebuild-contract
scripts/neri.sh run --project experiments/neri-data/providers/sqlite --unit schema-rebuild-contract --release
scripts/neri.sh run --project experiments/neri-data/providers/sqlite --unit schema-diff-contract
```

Primary references verified on 2026-09-23:

- SQLite, [Making other kinds of table schema changes](https://www.sqlite.org/lang_altertable.html#otheralter):
  specifies replacement creation, copying, dropping and final renaming within
  a transaction, with explicit handling of dependent schema objects. Neri
  rejects dependencies its model cannot describe and keeps foreign keys enabled.
- Microsoft, [SQLite provider limitations](https://learn.microsoft.com/en-us/ef/core/providers/sqlite/limitations#migrations-limitations):
  describes rebuild-based column changes and the boundary around artifacts
  represented in the model. Neri's supported constraint set remains smaller.
- SQLite, [Datatypes and affinity](https://www.sqlite.org/datatype3.html):
  destination affinity may convert stored values while other values retain their
  original storage class. Rebuild validation checks the resulting scalar domain.
- Curino, Moon and Zaniolo, [Automating Database Schema Evolution in Information
  System Upgrades](https://www.cs.cmu.edu/~tdumitra/hotswup09/papers/CurinoMoonZaniolo.pdf),
  HotSWUp 2009: explicit schema modification operators, data migration and
  retained evolution history motivate inspectable typed rebuild plans. This
  implementation does not include PRISM query rewriting or invertibility proofs.
