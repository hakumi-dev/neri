# Compilation projects

A version-2 `neri.json` contains named compilation units. A unit owns its
sources and can depend on library units without making namespaces depend on the
directory layout:

```json
{
  "version": 2,
  "defaultUnit": "web",
  "units": {
    "core": {
      "kind": "library",
      "sources": ["src/core"],
      "exclude": [],
      "references": []
    },
    "web": {
      "kind": "executable",
      "sources": ["app"],
      "exclude": [],
      "references": ["core"]
    }
  }
}
```

`defaultUnit` selects the unit used when the command does not pass `--unit`.
Every unit declares `kind` as `library` or `executable`. A local reference is
the name of another unit in the same manifest. An external reference names both
the other manifest and one of its units:

```json
{ "project": "../shared/neri.json", "unit": "core" }
```

References may target only library units. The complete reference closure is
deterministic: missing units, cycles and overlapping ownership of a canonical
source file are configuration errors. A diamond dependency is loaded once.

## Sources and exclusions

Each `sources` entry is relative to the manifest and is either an explicit
`.hk` file or a directory. Directories, including `.`, are discovered
recursively. `exclude` entries are literal relative files or directory
subtrees. Version 2 does not support globs.

Discovery skips symlinks, generated-output directories, and descendant
directories containing their own `neri.json`. The generated directories are
`.git`, `.neri`, `.cache`, `.idea`, `.bootstrap`, `build`, `out`, `dist`,
`target`, and `bin`. Manifest aliases resolve to canonical paths, but discovery
does not follow symlinks. `.hk` basenames must not contain whitespace, including
Unicode whitespace; directory names may contain spaces.

Source paths define compilation membership, not namespaces. Files in unrelated
folders may declare the same namespace, and folder names never create or infer
namespaces. `namespace` and `use` resolve names inside the selected unit and its
explicit reference closure; they do not add dependencies.

Libraries must not declare `main`. Executable units must declare exactly one
`main`; additional entry points are rejected by the binder.

## Command line

```sh
neri check --project neri.json --unit core
neri build --project neri.json --unit web --output build/web
neri run --project neri.json --unit web
```

`--unit` requires `--project` and overrides `defaultUnit`. Explicit source
arguments cannot be combined with `--project`. Compiler flags such as
`--release`, `--target`, and `--output` remain independent of source membership.

## Standard-library sources

The compiler and language server resolve the leading identifier of a `use`
declaration against `<NERI_STDLIB>/<identifier>.hk`. Available library sources
are loaded once, including their transitive imports and cycles. Names supplied
by the program or built-in libraries use ordinary semantic resolution; unresolved
imports produce compiler diagnostics. The toolchain launcher sets `NERI_STDLIB`
to its own library directory.

## Language server

For each document, the server selects the nearest ancestor `neri.json`, stopping
at the workspace root for documents inside it. Nested manifests are independent
project boundaries. Within a v2 manifest, a source is analyzed in the unit that
owns it. This is important for shared code: opening a library source selects the
library itself, not an arbitrary executable that references it. A file outside
all declared units is analyzed independently.

The selected unit is analyzed with its transitive library references and the
standard library. Open documents supply their current in-memory contents,
including dependencies; other members are read from disk. Editing an open
dependency reanalyzes its open consumers, and closing it restores the disk
contents for subsequent analysis.

A `workspace/didChangeWatchedFiles` notification reloads manifests and automatic
directory membership. Clients must report create, change and delete events for
closed `.hk` files, source directories, and relevant `neri.json` files. This
makes newly created or removed sources, exclusions, references, and nested
project boundaries take effect without restarting the server.

Compilation units are a source-graph description, not a package manager or a
general editor indexing root. The compiler currently flattens the selected
unit's reference closure into one analysis. It does not provide dependency
artifacts, version resolution, fetching, incremental graph caching, or
background cancellation.
