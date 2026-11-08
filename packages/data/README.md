# Neri Data source

[Neri Data](../../docs/DATA.md) describes the public API, distribution and
supported behavior. This directory owns the provider-independent runtime and
the [SQLite](providers/sqlite/manifest.json) and
[PostgreSQL](providers/postgresql/manifest.json) libraries.

The [runtime manifest](manifest.json) owns one library. Its source directories
group files by responsibility.

- `model/` defines scalar values, codecs and mapped model metadata.
- `query/` translates and executes typed query plans.
- `navigation/` loads and maintains relationships.
- `persistence/` manages tracked state, writes and transactions.
- `schema/` models schemas and plans migrations.

The runtime and providers share the [provider contract](docs/PROVIDERS.md).
The [generator and migration tools](../../tooling/data/manifest.json) use the
runtime libraries. The [Data tests](../../tests/data/README.md) own model
fixtures and verification projects.
