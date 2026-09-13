# Declaration templates

Declaration templates combine catalog data with compiler-backed providers.
The catalog defines completion entries and editable text. Providers determine
where a declaration belongs and preserve the syntax already written. Inherited
override signatures come from resolved compiler symbols.

## Catalog discovery

The launcher supplies `NERI_TEMPLATE_CATALOG`. Its default is
`share/neri/templates/declarations.json` in the installed toolchain. The source
launcher `scripts/neri.sh` uses that path under the checkout. An explicit
environment value selects a custom catalog. A direct compiler invocation can
also discover the bundled catalog beside the directory identified by
`NERI_STDLIB`.

The language server loads and validates the catalog when it starts. To add an
entry in a supported context, edit a custom copy, select it with
`NERI_TEMPLATE_CATALOG`, and restart the server. The compiler and editor plugin
do not need rebuilding. An already running server keeps its validated catalog.
Invalid data disables catalog templates and reports the file, line and column
through an LSP error message. Semantic services remain available.

The package includes `declarations.json` and `declarations.schema.json` as
runtime assets. Both remain present in installations made with `--no-doc`.
Custom catalogs are complete replacements for the bundled catalog.

## Format

A version 1 catalog has `version` and `templates` properties. Each entry has
`id`, `prefix`, `label`, `detail`, `context`, `provider`, `header` and `body`.
IDs are unique. Unknown properties, providers and contexts are errors.
`prefix` is the completion trigger; `header` supplies the emitted declaration.

Contexts are `global` and `class`. Providers are:

- `classDeclaration`: a class header with continuation of written syntax.
- `functionDeclaration`: a function or method header with continuation of
  written names, type parameters, parameters and annotations.
- `staticDeclaration`: a complete declaration expanded from its trigger.

The class and function providers require the declaration name to be one
editable field. Their headers begin with the declaration keyword; written
modifiers come from the current document. Use a static declaration for a fixed
header containing modifiers. Static declarations apply while typing the trigger
itself. Editable fields remain inside the header regions that can be retained or
completed independently.

`header` is one logical line. `body` has a `kind` and `lines`. A `block` body
contains interior lines; the insertion engine supplies the surrounding newline,
indentation and owned `end`. A `none` body has an empty `lines` array.
Leading tabs in body lines represent indentation levels relative to the body;
the editor's configured indentation determines their physical width. Leading
spaces are rejected so indentation stays controlled by that configuration.

Template text supports `${N:default}` editable fields numbered consecutively
from 1 and at most one `$0` final caret. `\$`, `\\` and `\}` represent literal
characters at the template layer. JSON escaping applies outside that layer;
Neri string escaping applies to any string literal in the resulting program.
Completion renumbers the remaining editable fields after retaining written
syntax. Plain-text clients receive field defaults and no caret markers.

The loader compiles template text into literal, field and final-caret parts.
It parses the expansion with field defaults through the Neri parser to validate
its declaration structure. The published JSON schema describes the data shape;
the Neri validator additionally checks IDs, placeholder ordering, provider
compatibility and declaration syntax. References to types and members in the
inserted code are checked by normal semantic analysis.

## References

- Amorim, Erdweg, Wachsmuth and Visser,
  [Principled Syntactic Code Completion using Placeholders](https://doi.org/10.1145/2997364.2997374),
  SLE 2016, describes syntax-derived completion and an explicit representation
  of incomplete programs. It motivates validating template expansion against
  language syntax. Neri uses its existing parser; this catalog is not an
  implementation of the paper's grammar-derived completion framework.
- Omar, Voysey, Hilton, Aldrich and Hammer,
  [Hazelnut: A Bidirectionally Typed Structure Editor Calculus](https://arxiv.org/abs/1607.04180),
  POPL 2017, formalizes meaningful incomplete terms and context-sensitive edit
  actions. It motivates preserving semantic context during editing. Its
  metatheory applies to its structure-editor calculus, not to Neri's textual LSP.
- The [Visual Studio snippet schema](https://learn.microsoft.com/en-us/visualstudio/ide/code-snippets-schema-reference?view=visualstudio)
  separates metadata, editable declarations and code; the
  [SquareRoot walkthrough](https://learn.microsoft.com/en-us/visualstudio/ide/walkthrough-creating-a-code-snippet?view=visualstudio)
  demonstrates a data-defined editable value referenced in a body.
- [Rider live templates](https://www.jetbrains.com/help/rider/Creating_a_Live_Template.html)
  configure availability, text, variables and formatting separately.
- Roslyn's [OverrideCompletionProvider](https://github.com/dotnet/roslyn/blob/main/src/Features/CSharp/Portable/Completion/CompletionProviders/OverrideCompletionProvider.cs)
  consumes syntax and semantic symbols to select inherited members. It supports
  retaining override decisions in a semantic provider.
- [LSP 3.17 completion](https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_completion)
  defines snippet negotiation, editable stops, final caret position, text edits
  and incomplete completion lists. The client interprets the resulting snippet.

JSON is Neri's catalog format. These references support the separation of
declarative content and contextual behavior; they prescribe neither JSON nor
Neri's provider interfaces. Performance claims require measurements of Neri.
