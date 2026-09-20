# Neri documentation

These documents describe the source revision that contains them. Toolchain
packages include the language reference for the packaged compiler.

## Find a contract

Start with the matching task or identifier below, then follow the section's
example and verification references. Repository paths refer to this checkout.

| Task or identifier | Authoritative section |
| --- | --- |
| Call C: `@cabiImport`; expose Neri to C: `@cabiExport`; `cabi fn` | [C interoperability](C-INTEROP.md) |
| Numeric overflow, null refinement, `readonly` | [Values and variables](LANGUAGE.md#values-and-variables) |
| `contract`, generic constraints, explicit type arguments | [Generics](LANGUAGE.md#generics) |
| Named call arguments and evaluation order | [Named arguments](NAMED-ARGUMENTS.md) |
| `shared fn`, captures, `task`, `parallel` | [Function values and closures](LANGUAGE.md#function-values-and-closures) |
| `manifest.json`, `projects` versus `references` | [Workspace members](PROJECTS.md#workspace-members) |
| Test registration, `{project}`, `{compiler}`, `{work}` | [Registered project tests](PROJECTS.md#registered-project-tests) |
| MCP tool choice, overlays versus saved sources | [Agent tools](AGENT-FEEDBACK.md#tools) |
| Stale `expectedRevision`, UTF-16 edits, snapshot digests | [Revisions and snapshots](AGENT-FEEDBACK.md#revisions-and-snapshots) |
| Source to IR to native code; component ownership | [Architecture](ARCHITECTURE.md) |
| Runtime layouts, roots, ABI versions and features | [Runtime and IR ABI](ABI.md) |
| Stage0, fixed point, `refresh-seed`, seed provenance | [Bootstrapping](BOOTSTRAP.md) |

## Use Neri

| Task | Reference |
| --- | --- |
| Install and write a first program | [Getting started](../README.md#install), [examples](../examples/README.md) |
| Look up syntax, types and commands | [Language](LANGUAGE.md), [projects](PROJECTS.md) |
| Format and debug code | [CodeStyle](CODESTYLE.md), [debugging](DEBUGGING.md) |
| Work with text and collections | [Text](TEXT.md), [buffers](BUFFERS.md), [results](RESULT.md) |
| Inspect typed expressions | [Quotations](QUOTATIONS.md) |
| Accept typed field arguments | [Fields](FIELDS.md) |
| Call C libraries and export Neri functions | [C interoperability](C-INTEROP.md) |
| Connect an agent to compiler feedback | [Agent feedback](AGENT-FEEDBACK.md) |
| Access files and watch changes | [Files](FILES.md), [change scanner](CHANGES.md) |
| Run processes and build consoles | [Processes](PROCESS.md), [terminal](TERMINAL.md), [compiled sessions](SESSIONS.md) |
| Serve HTTP and make local requests | [HTTP](HTTP.md) |
| Measure time and use cryptography | [Clocks](CLOCK.md), [cryptography](CRYPTO.md) |

## Develop Neri

Start with [building and supported platforms](BUILDING.md) and
[testing](../tests/README.md). Platform setup is detailed for
[Linux](LINUX.md) and [Windows](WINDOWS.md).
See [packaging](PACKAGING.md) for distribution and standalone installation.

## Internals

- [Architecture](ARCHITECTURE.md): compiler, libraries and native boundaries.
- [Runtime and IR](ABI.md): layouts, memory management and ABI contracts.
- [Language server](LANGUAGE-SERVER.md): editor protocol and semantic services.
- [Generated sources](GENERATED-SOURCES.md): validated artifacts, snapshots and source provenance.
- [Declaration templates](TEMPLATES.md): catalog format, contextual providers and editor insertion.
- [Bootstrapping](BOOTSTRAP.md): trusted seed and reproducible generations.
- [Performance](PERFORMANCE.md): workloads, measurements and budgets.

Bugs and proposals are tracked in [GitHub issues](https://github.com/hakumi-dev/neri/issues).
