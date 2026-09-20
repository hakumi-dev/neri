# Neri CodeStyle

Neri CodeStyle shares one syntax-based rule engine between the compiler CLI and
the language server. Rules inspect the compiler's syntax, block metadata and
tokens, report diagnostics and propose safe layout edits against the original
source text.

## Commands

```sh
neri format source.hk
neri format --check --project manifest.json
neri lint --project manifest.json --unit compiler-core
neri lint --fix source.hk
```

With no source arguments, commands discover the project in the current
directory. Project commands visit the sources owned by its declared units;
`--unit` selects one unit. Canonical paths are deduplicated. Referenced external
projects retain their own formatting policy and are processed separately.

Exit status `0` means success, `1` means `format --check` found changes or
`lint` has remaining diagnostics, and `2` means a usage, configuration, syntax,
project-loading or I/O failure.

`format` applies enabled layout rules. `format --check` reports whether those
rules require changes. `lint` reports rule diagnostics, and `lint --fix` applies
safe corrections before reporting remaining diagnostics. All input files are
read, parsed and checked before writing begins. Each changed file is written
atomically after checking that its content still matches the analyzed snapshot.
Writes form a sequence of per-file replacements. A later I/O failure reports
failure and leaves earlier successful replacements in place. POSIX replacements
preserve existing permission bits; ownership, ACLs and extended attributes are
outside that guarantee.

## Rules and configuration

| Rule | Behavior | Default |
| --- | --- | --- |
| `NRSTYLE001` | Separate a group of sibling `let`/`var` declarations from the following statement with a blank line. | Enabled, warning |
| `NRSTYLE002` | Separate an assertion group from a preceding non-assertion statement with a blank line. | Enabled, warning |
| `NRSTYLE003` | Remove trailing spaces and tabs, including after comments. | Enabled, warning |
| `NRSTYLE004` | Add a final newline to a nonempty file that lacks one. | Enabled, warning |
| `NRSTYLE005` | Indent parser-defined block bodies and parenthesized/bracketed continuations. | Enabled, warning |
| `NRSTYLE006` | Normalize horizontal spacing around commas, colons, dots, delimiter interiors, assignments, casts and syntax-resolved unary/binary operators. | Enabled, warning |
| `NRSTYLE007` | Report each class or enum declaration after the first top-level class or enum in a file. | Disabled, warning; enabled for `compiler/`, `tooling/` and `stdlib/` |

Consecutive declarations and consecutive assertions stay together. An assertion
at the start of a body needs no leading blank line. Closing delimiters end a
body; they do not begin another statement. Comments attached to the following
statement stay with it.

Indentation defaults to two spaces and follows `indent_style`, `indent_size`
and `tab_width`. `indent_size = tab` uses `tab_width`. Inline bodies keep their
existing line structure. A delimiter continuation adds one indentation level;
its closing delimiter returns to the enclosing level. Blank lines retain their
number, and trailing-whitespace cleanup removes their spaces and tabs.
With tab indentation, tabs cover `tab_width` columns and any remainder uses
spaces. An unspecified indentation size uses the tab width for tab indentation.

Token spacing distinguishes unary operators, binary operators, ternary colons,
type annotations and named arguments. Generic angle brackets and optional/pointer
type suffixes retain their existing spacing. Literals retain their exact spelling.
Existing line endings are preserved; an inserted newline uses the file's detected
line ending. Empty files remain empty.

Configuration lives in `.editorconfig` and follows its directory hierarchy,
`root = true`, section matching, property precedence and `unset` behavior.

```ini
root = true

[*.hk]
indent_style = space
indent_size = 2
trim_trailing_whitespace = true
insert_final_newline = true
neri_blank_line_after_declarations = true
neri_blank_line_before_assertions = true
neri_indentation = true
neri_token_spacing = true
neri_one_class_per_file = false
neri_diagnostic.NRSTYLE001.severity = warning
neri_diagnostic.NRSTYLE002.severity = warning
neri_assertion_helpers = test/assert*, assert

[compiler/**/*.hk]
neri_one_class_per_file = true

[tooling/**/*.hk]
neri_one_class_per_file = true

[stdlib/**/*.hk]
neri_one_class_per_file = true
```

Severity values are `none`, `suggestion`, `warning` and `error`. Assertion helper
names are case-sensitive. Use `/` between name components in configuration:
`test/assert*` matches calls such as `test::assertTrue()` by their canonical
qualified name, which starts with `test.assert`.
A final `*` matches a name prefix. Each matching section replaces the whole
helper list; `unset` restores the defaults (`test/assert*`, `assert`).
Neri also accepts dot-qualified values. Slash qualification keeps each helper
as one value identifier in Rider's EditorConfig parser.
Severity `none` suppresses reporting while keeping the formatting preference.
Set a rule's boolean option to `false` to disable its analysis and correction.
NRSTYLE007 has no automatic correction because moving declarations can change
source ownership and project structure.

## Source organization

Namespace separators have no surrounding spaces: `App::Nested::Type`.
Member access also has no surrounding spaces: `App::Type.create()`.
Namespace aliases use assignment spacing: `use IO = App::Nested`.

Compiler, tooling and standard-library sources keep one top-level class or enum
per file, grouped in directories by responsibility. Extracted types use their full name in
snake_case, for example `IrFunction` in `compiler/ir/ir_function.hk`. Related
top-level functions can share a file. The project manifest determines source
ownership and references across units.

The repository enables NRSTYLE007 for `compiler/`, `tooling/` and `stdlib/`.
Tests can keep multiple types together to express a language contract.
Standard-library modules declare their source files in `stdlib/manifest.json`.

## Editor integration

The LSP publishes rule identifiers and source ranges as diagnostics.
`textDocument/formatting` returns minimal text edits. Clients supporting
code-action literals and versioned workspace edits receive individual quick
fixes and `source.fixAll.neri` for the current document. The client applies these
edits against the advertised document version.
The Rider client watches project `.editorconfig` files and asks the server to
refresh diagnostics after changes. Its SDK provides the LSP formatting and
intention-action UI.
The plugin's optional EditorConfig integration registers Neri property
descriptors for key completion and value validation. A client version without
those descriptors reports “The property is not supported” for Neri keys.

`initializationOptions.codeStyleDiagnostics = false` disables automatic style
diagnostic publication for that client. Explicit formatting and correction
requests remain available. The default is `true`.

## Extending the engine

Implement a `NeriCodeStyleRule` in Neri and register it in
`neriCodeStyleRules()`. `NeriCodeStyleRule` is abstract, and every concrete rule
implements `evaluate` as required by the compiler. A rule supplies an identifier,
option key, category, default severity and typed findings and edits.
`validateOption` defines the accepted values for its configuration key.
The CLI and LSP consume those descriptors and results through the shared
engine. Add a behavior contract for the rule's intended change and important
false-positive boundary.

Edits retain the original source outside their ranges. The layout engine accepts
non-overlapping UTF-16 whitespace replacements. Before adapters offer or apply
corrections, it checks token spelling and values, syntax-tree structure and
comment content, allowing comment trailing whitespace to follow the cleanup rule.
These checks preserve statement boundaries as well as literals. Coincident
blank-line and indentation changes share one edit. Contracts cover idempotence,
overlapping rule requests, multiline expressions, comments and UTF-16 coordinates.

## Repository checks

`scripts/build.sh test` checks formatting and lints owned project sources with
the freshly bootstrapped compiler before running the language contracts.
The CI jobs that run this entry point,
including packaging, enforce the same checks. Its scope covers the root manifest,
ABI tooling and Neri Data generation/runtime/verification/example manifests.
The standard-library manifest receives the same formatting and lint checks.
Manifest-owned sources are deduplicated and declared generated outputs
are excluded. Parser fixtures outside these units retain their purpose-specific
layout; source snippets inside test literals remain unchanged.

## Verified references

- [.NET runtime source-file guidelines](https://github.com/dotnet/runtime/blob/main/docs/coding-guidelines/project-guidelines.md)
  provide a precedent for one class per file, type-based filenames and directory
  organization. Neri applies its own configurable scope and snake_case naming.
- [Effective Go: formatting](https://go.dev/doc/effective_go#formatting)
  supports a shared automated formatter as the project's layout convention.
- [Microsoft coding conventions](https://learn.microsoft.com/en-us/dotnet/csharp/fundamentals/coding-style/coding-conventions)
  illustrates configurable style analysis and CI enforcement. Neri retains its
  own syntax and indentation conventions.
- [Roslyn analyzer and code-fix tutorial](https://learn.microsoft.com/en-us/dotnet/csharp/roslyn-sdk/tutorials/how-to-write-csharp-analyzer-code-fix)
  grounds the separation between rule diagnostics and proposed corrections.
- [Roslyn syntax model](https://learn.microsoft.com/en-us/dotnet/csharp/roslyn-sdk/work-with-syntax)
  explains why tokens and trivia matter for source-preserving transformations.
  Neri retains its original source alongside the compiler's syntax and tokens.
- [EditorConfig specification](https://spec.editorconfig.org/)
  defines configuration discovery, matching and precedence.
- [JetBrains EditorConfig grammar](https://github.com/JetBrains/intellij-community/blob/f1f6e824dca999f1543601821ebd12537fd483c2/plugins/editorconfig/common/EditorConfig.bnf#L150)
  defines the value identifiers accepted by Rider. Slash qualification is a
  Neri convention compatible with that grammar; EditorConfig itself leaves
  custom property values to their consumers.
- [LSP formatting contract](https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_formatting)
  defines the editor request and returned text edits;
  [code actions](https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_codeAction)
  define quick fixes, source actions and capability negotiation.
- Philip Wadler, [A prettier printer](https://homepages.inf.ed.ac.uk/wadler/papers/prettier/prettier.pdf),
  develops compositional formatting and the separation of document structure
  from layout. It provides a foundation for future layout rules; the current
  spacing engine does not implement its width-dependent pretty-printing algorithm.
- [JetBrains LSP integration](https://plugins.jetbrains.com/docs/intellij/language-server-protocol.html)
  documents the client's formatting, intention actions and watched-file support.

References motivate the design. Executable Neri contracts verify this
implementation's behavior.
