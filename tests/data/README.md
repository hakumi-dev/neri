# Neri Data tests

The [Data source package](../../packages/data/README.md) owns the runtime
and provider implementations. This directory owns their contracts and fixtures.

- The [runtime contract project](manifest.json) exercises provider-independent
  behavior and supplies shared contracts to provider tests.
- `providers/` holds SQLite and PostgreSQL contracts. `provider-contract/`
  exercises the query and persistence APIs with SQLite.
- Each model fixture keeps its mapping, source entities, generated artifacts
  and contracts together. The generator manifest verifies the input and output
  digests for each generated tree.
- `tooling/` tests generation and migration tooling. The root manifest
  registers the cross-provider model contracts.
- `package-consumer/` is copied to `examples/data-consumer/` inside a toolchain
  package. Its manifest uses the installed Data libraries.

## Generate and run the sample fixture

From the repository root, run the generator before building the sample.

```sh
scripts/neri.sh run --project tooling/data --unit generator -- \
  "$PWD/tests/data/generated-example/mapping.json"
scripts/neri.sh run --project tests/data/generated-example --unit sample
```
