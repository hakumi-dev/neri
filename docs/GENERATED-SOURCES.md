# Generated source contracts

A version 2 project unit can declare generation manifests in `generated`:

```json
{
  "kind": "library",
  "sources": ["database.hk"],
  "generated": ["generated/manifest.json"],
  "references": ["entities"]
}
```

The shared project loader validates each selected unit's manifests and adds their
output files to that unit. Generated declarations then use ordinary parsing,
binding, generic inference and semantic editor queries. A generator runs
explicitly before consumers load its output. Selecting an independent input unit
allows regeneration when a consuming unit's output is missing or outdated.

A generated-only unit declares an empty `sources` array and at least one
generation manifest:

```json
{
  "kind": "library",
  "sources": [],
  "generated": ["generated/manifest.json"]
}
```

Ordinary source ownership follows each unit's `sources` and `exclude` entries.
Generated source ownership follows the validated output paths declared by that
unit's generation manifests, including for generated-only units. A generated
output may also be listed as an ordinary source of the same unit. A path owned by
different units, or declared as generated output by multiple units, is rejected
as ambiguous.

## Generation manifest

```json
{
  "version": 1,
  "generator": "example-generator",
  "generatorVersion": "1",
  "inputs": [{"path": "entities.hk", "sha256": "<64 lowercase hex digits>"}],
  "outputs": [{"path": "generated/revision/entities.hk", "sha256": "<64 lowercase hex digits>"}],
  "origins": [{
    "path": "generated/revision/entities.hk",
    "start": 6,
    "length": 12,
    "source": "entities.hk",
    "sourceStart": 6,
    "sourceLength": 8,
    "identity": "type:app.Customer"
  }]
}
```

Paths are relative to the containing unit's project root. Input paths may refer
to parent directories. `@stdlib/name.hk` resolves against the configured standard
library and retains its content digest across toolchain locations. Output paths
are normalized project-relative `.hk` files within the generation manifest's
directory; their canonical paths preserve that containment without symlinks.
Changing the content of a declared standard-library input requires regeneration,
including when selecting another toolchain version.

Version, properties, duplicate keys, digests and origin spans are validated.
Inputs and outputs are nonempty arrays. Each origin connects a declared output
to a declared input, using source-local UTF-16 offsets and a nonempty semantic
identity. Unknown versions or properties, unavailable artifacts and digest
mismatches produce a project preparation error with a regeneration instruction.

The loader retains the exact input, output and generation-manifest text that it
validated. Origins, CLI binding and retained sessions consume that snapshot.
The language server also checks open generated-output buffers against it before
using those declarations. A file change after project loading cannot substitute
unchecked text into that prepared generation.

## Editor and session snapshots

Definitions resolve to emitted declarations. The `neri/sourceOrigin` LSP
extension accepts the same text-document position parameters as a definition
request and returns the original `Location`, or `null` when there is no mapped
origin. The server advertises `experimental.sourceOriginProvider`. Resolution
first identifies the generated declaration through the semantic index and then
selects its smallest enclosing origin span. Origin locations use text retained
with the validated manifest.

An open generation input whose text differs from the recorded digest suppresses
consumer semantic analysis and reports `NR_GENERATED`. Saving the input and
regenerating restores the consumer API on reanalysis. File-change notifications
invalidate project analyses. Retained-session configuration identities include
generation manifest contents, alongside ordinary source and project inputs.

Editing a generated output in an open buffer also reports `NR_GENERATED` and
suppresses consumer semantics until the buffer matches the published artifact.

Generation manifests define preparation-time content checks. Their atomic
publication selects a complete set of immutable output paths. A publisher may
remove obsolete outputs after selecting a new manifest; readers holding the old
manifest can then fail preparation and retry against the new generation.

## Basis and verification

[Build Systems à la Carte](https://www.microsoft.com/en-us/research/wp-content/uploads/2018/03/build-systems.pdf)
(Mokhov, Mitchell and Peyton Jones, ICFP 2018, DOI `10.1145/3236774`) separates
dependency scheduling from decisions about rebuilding and defines correctness
in terms of current task inputs. This motivates explicit input inventories and
content checks; Neri's manual generation workflow implements its own bounded
contract.

[ECMA-426](https://tc39.es/ecma426/2024/) specifies mappings between generated
and original source locations. Neri's manifest uses an independent schema with
semantic identities and UTF-16 spans. It shares the source-location principle;
it is not an ECMA-426 source map. Both primary references were checked on
2026-09-12.

`generated-source-contracts` exercises selected-unit loading, digest failures,
generated membership, original and generated navigation, unsaved editor inputs,
regeneration and session configuration invalidation:

```sh
scripts/neri.sh run --project . --unit generated-source-contracts -- "$PWD"
```
