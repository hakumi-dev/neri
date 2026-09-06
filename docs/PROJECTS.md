# Compilation projects

A version-1 `neri.json` describes one project for the compiler CLI and language
server. With no `sourceSets`, it discovers `.hk` files recursively from the
manifest directory on every analysis:

```json
{ "version": 1 }
```

Discovery skips symlinks and directories named `.git`, `.neri`, `.cache`,
`.idea`, `.bootstrap`, `build`, `out`, `dist`, `target` and `bin`. A descendant
directory containing its own `neri.json` is a nested-project boundary and is
not discovered by its parent. Manifest aliases resolve to their canonical paths;
source discovery does not follow symlinks. `.hk` basenames must not contain whitespace, including Unicode
whitespace; directory names may contain spaces.

`sourceSets` remains the advanced, compatible form for projects that need named
compilation units:

```json
{
  "version": 1,
  "defaultSourceSet": "application",
  "sourceSets": {
    "application": ["src/main.hk", "src/app.hk"],
    "tests": ["tests/main.hk", "src/app.hk"]
  }
}
```

Source entries are relative to the manifest directory. An entry can be an
explicit `.hk` path, `directory/*.hk` for that directory only, or
`directory/**/*.hk` for that directory and its descendants. The directory
prefix is literal: `?`, wildcard directory names, root-wide `**/*.hk`, absolute
paths and `.` or `..` segments are rejected. Each glob is sorted; an individual
glob may match no files, but the resolved source set cannot be empty. Duplicate
paths are rejected. Without an explicit source set, automatic discovery is the
project's single source set.

## References and libraries

`references` optionally names relative directories containing another
`neri.json`:

```json
{
  "version": 1,
  "references": ["../shared", "libraries/collections"]
}
```

References reject absolute paths, backslashes, empty components and `.`
components; a relative parent reference such as `../shared` is valid. Every
target must have its own manifest. A referenced project uses its own automatic
discovery or own default source set; it never inherits the parent's
`--source-set` selection.
The complete reference closure is deterministic: cycles are errors, diamonds
are loaded once, and a duplicate canonical source path is an error.

Referenced projects are source-only libraries and must not define `main`. The
root project's own sources may omit `main`, which is valid for `check` and the
language server; executable `build` and `run` require a root entry point.
Non-executable emission, such as `build --emit=obj`, also permits libraries.
Multiple root `main` declarations are errors. `namespace` and `use` resolve
names and standard libraries; they never infer user-project dependencies.

## Command line

```sh
neri build --project neri.json --output build/application
neri run --project neri.json --source-set tests
neri check --project neri.json
```

`--source-set` overrides `defaultSourceSet` and requires `--project`. Explicit
source arguments cannot be combined with `--project`. Existing commands with
explicit source arguments retain their behavior. Compiler flags such as
`--release`, `--target` and `--output` remain independent of source membership.

## Language server

The server selects the nearest ancestor `neri.json` for each document, stopping
at the workspace root for documents inside it. A workspace can contain several
independent projects. The automatic source set is used when `sourceSets` is omitted. Otherwise the default
set is preferred for member files; a unique matching set is selected for another
member, and multiple matches require explicit selection through
`initializationOptions.sourceSet` for the workspace-root manifest. Nested projects
use their own defaults and automatic selection. Referenced source-only libraries participate
transitively. Files outside the selected project sources are analyzed
independently.

Open documents supply their current in-memory contents, including unsaved edits;
other members are read from disk. Changes and document close reanalyze open
documents. Closing an unsaved dependency restores its disk contents for dependent
analysis. A `workspace/didChangeWatchedFiles` notification triggers reanalysis
and reloads automatic membership, source sets and references from disk. Clients
must deliver create, change and delete notifications for closed `.hk` files,
source directories, and every relevant `neri.json`; this lets new or removed
matching files and nested-project boundaries take effect without restarting the
server.

Project diagnostics use per-file ranges and document versions. Hover, definition
and references retain their documented symbol coverage; multi-file compilation
does not imply navigation for all symbol kinds. Missing sources or invalid
configuration produce project diagnostics and invalidate semantic models.

Source membership is not a package manager or editor indexing root. Each project
closure is reanalyzed as a unit; dependency-graph caching and background
cancellation are unsupported.
