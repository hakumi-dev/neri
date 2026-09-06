# Neri language server

Run the installed toolchain launcher with `neri lsp`. It speaks LSP over stdio;
stdout contains only UTF-8 JSON-RPC frames with byte-counted `Content-Length`.
The server is implemented in `compiler/lsp/*.hk` and shipped in the same native
executable as the compiler. No Python, Node, editor SDK or separate server
installation is required at runtime. Protocol tests use a native C client,
built with the project's existing Clang/CMake infrastructure.

The editor-independent server belongs in **neri**. The separate **neri-rider**
project owns the JetBrains client, grammar, configuration UI and Run/Build/Check.

## Implemented contract

- `initialize`, `initialized`, `shutdown`, `exit` and JSON-RPC error replies.
- Incremental document synchronization (`textDocumentSync.change = 2`): open,
  ranged/full changes and close. Ranges use UTF-16, CRLF counts as one line
  break, and surrogate pairs cannot be split. Changes in a notification apply
  sequentially; invalid batches preserve both the previous text and version.
  Text is analyzed in memory; the server does not save or execute it.
- Parser and binder diagnostics from the compiler's actual `BootstrapParser`
  and `BootstrapBinder`. No CLI-output parsing or TextMate-based type inference.
- UTF-16 ranges, including non-BMP characters, tabs and CRLF line endings.
- Versioned `publishDiagnostics`; old/duplicate change versions are ignored.
  Successful analysis and document close publish an empty diagnostic list.
- The same `http`, `terminal`, `clock` standard-library sources as the CLI,
  located using the launcher's `NERI_STDLIB` environment.
- Hover for resolved variable/parameter declarations and reads, `this` and literals, using the
  bound model's types and UTF-16 ranges. Unresolved names and ambiguous generic
  instantiations return no hover. Invalid syntax invalidates the retained model.
- Definition and references for local variables and parameters within a
  document, including reads and assignments. Declaration locations identify the
  name, not the declaration keyword. Queries use binding identity, distinguish
  same-spelling symbols and honor `includeDeclaration` for references.
- Document highlights use the same binding identities and remain within the
  queried document. Highlights use the text kind; read/write classification
  is not provided.
- Document symbols return flat `SymbolInformation` entries ordered by source
  position, using compiler declarations and identifier ranges. Generated
  specializations are excluded. Hierarchical declaration ranges are not provided.
- Completion resolves active locals, parameters, receiver members, inherited
  members, static methods, classes and intrinsic library functions through the
  compiler. Visibility and lexical scopes filter candidates. Replacement edits
  cover the complete identifier using UTF-16 ranges, including mid-word queries.
  Bare member access is analyzed using a marked copy of the current text;
  unsupported repairs and ambiguous specializations produce no candidates.
  Function and method details contain signatures without the `def` keyword;
  completion kinds identify functions, methods, classes, fields and variables.
- `completionItem/resolve` supplies documentation on demand. Items identify the
  document version and analysis revision; changes invalidate earlier requests
  for enrichment. Types and insertion edits do not depend on documentation.
- Signature help uses resolved calls, constructors and intrinsic contracts,
  with parameter labels and the active argument. Nested calls and commas inside
  literals are distinguished. A limited delimiter repair supports incomplete
  calls without reusing old semantic models. Unresolved inner calls do not
  fall back to the outer signature. When the argument index exceeds the
  declared parameters, the active parameter is omitted.
- `.hk` basenames containing whitespace produce `NR_FILE_NAME`; directories
  containing spaces remain supported. See [source naming rules](PROJECTS.md).
- Class construction and direct function calls support definition and references
  across source-set members, including closed files. Hover displays the resolved
  class name or callable signature. Source-map locations preserve each file's
  URI and local UTF-16 ranges. Generic instantiations are not covered by this
  contract.
- Fields and resolved method calls use the declaring owner's identity, including
  inherited members, static calls and explicit base calls. Field hover reflects
  the compiler's receiver-adjusted type. Callback parameters, callback-local
  declarations and repeated captures retain their original source identities.

With a [compilation project](PROJECTS.md), each document is analyzed alongside
its automatic or selected source-set members, transitive source-only references,
and standard libraries. A manifest without `sourceSets` discovers `.hk` files
recursively while excluding generated directories, symlinks and nested projects.
References are explicit relative manifest directories; `use` and `namespace` do
not infer project dependencies. Open dependencies use their unsaved contents.
Without a matching project source set, the document is an independent compilation
unit and references to other user files can be unresolved. The server recomputes
membership and references after `workspace/didChangeWatchedFiles`; it does not
combine unrelated programs and negative fixtures into one compilation unit.
Library roots without `main` remain valid for analysis; executable commands
require the root project to define its own `main`. See [project sources and
references](PROJECTS.md).

Analysis is synchronous and ordered. Diagnostic versions let clients discard
obsolete results, but there is no background cancellation, debounce,
incremental semantic cache or incremental analysis yet. The bound model for
each analyzed document is retained for semantic queries. A large analysis can
delay later messages.
Transport limits are 2 MiB per message, 8 KiB of headers and 64 nested JSON
containers. Malformed framing terminates the session; malformed JSON gets a
parse-error reply. Protocol notices use `window/logMessage`.

Rename and semantic tokens are not advertised or implemented. Type-annotation
navigation and general generic navigation are unsupported. Completion and signature repairs do not
provide general error-tolerant analysis. Background cancellation is unsupported.
See [acceptance criteria](LSP-ACCEPTANCE.md)
for language-service quality requirements.

## Optional symbol documentation

Consecutive `##` comment lines immediately before a declaration provide its
documentation. Comment indentation must match the declaration; a blank line
breaks the association. Symbol resolution comes from the compiler, not comment
text. Documentation is plain prose, rendered safely using the client's supported
hover or completion format.
Functions, methods, classes, fields and native records support this association.
Parameter-specific tags and documentation for local variables are not interpreted.

```neri
## Returns a greeting for the supplied name.
def greeting(name: String): String
  return "Hello, " + name
end
```

The initialization option `documentation: false` disables explanatory text while
preserving types, signatures, completion and navigation. Documentation is enabled
by default. Clients control whether hover or completion help appears automatically
or only when requested.

Installed standard-library documentation comes from the optional
`stdlib/documentation.json` index, generated from compiler-resolved declarations
and checked against source hashes. Missing or incompatible documentation does not
prevent compilation or language-service analysis. Project source documentation
is read from its current in-memory sources, including unsaved changes.

The package installer accepts `--no-doc` to omit the index and standalone manuals;
required standard-library `.hk` sources remain installed. See
[package installation](PACKAGING.md). This option does not remove comments from
project sources or change program binaries.

## Implementation constraints

The server and protocol tests use Neri and C and the compiler's existing native
toolchain. Transport and semantic analysis have no editor-specific dependencies.
Semantic features use compiler symbol identity, types and source ranges rather
than inferring meaning from syntax coloring or CLI output.

Streaming input uses bounded byte-array chunks, decoded at UTF-8 boundaries,
and a balanced string join. Repeated `host.appendByte` on an ever-growing array
copies the previous contents on each append and causes quadratic copying.

## Validate

`scripts/build.sh test` runs the protocol suite against the freshly bootstrapped
compiler, alongside existing compiler and native tests. To run it separately:

```sh
build/native/native-release/neri-lsp-test "$HOME/.neri/bin/neri" "$PWD"
```

The tests exercise framing/lifecycle, recovery, real type errors, incomplete
blocks, the existing callback fixture, standard-library loading, Unicode ranges,
unsaved changes, stale versions, diagnostic clearing and binding-based hover,
definition and references. They do not substitute
for checking visible diagnostics and correction in an actual client editor.

### Diagnostic latency baseline

The same C client accepts an optional `--benchmark` after the source root. It
runs the contracts first, then measures 256 sequential, one-character ranged
edits in a comment prepended to the existing callback contract (a real program,
not an empty document). Each edit must produce empty diagnostics with its exact
version. The current server still reparses and binds the whole document.

```sh
build/native/native-release/neri-lsp-test /path/to/current/compiler "$PWD" --benchmark
```

Output reports nearest-rank p50/p95/p99 and maximum milliseconds from sending
the change to receiving and validating diagnostics. Linux RSS samples come
from the server's `/proc/<pid>/status`, before/after the loop and after each
response; `-1` means unavailable. Sampling is outside the timed interval. The
sampled peak is not an OS high-water mark. The process already handled the
contract fixtures, so this is a warm single-document baseline, not startup,
multi-project, concurrent typing, editor-rendering or prolonged-session proof.
There are no performance pass/fail thresholds yet. Compare release binaries on
the same host and record compiler SHA, fixture size and host/load with results.

## Editor configuration

Configure a compatible LSP client to launch `neri lsp` over stdio using the
toolchain launcher. Clients can negotiate incremental synchronization; full
replacements remain valid change events. File membership and process lifecycle
are client responsibilities. Consult the client integration's documentation for
editor-specific settings and execution actions.

Protocol reference: [LSP specification](https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/).
