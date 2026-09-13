# Neri documentation

These documents describe the source revision that contains them. Toolchain
packages include the language reference for the packaged compiler.

## Use Neri

| Task | Reference |
| --- | --- |
| Install and write a first program | [Getting started](../README.md#install), [examples](../examples/README.md) |
| Look up syntax, types and commands | [Language](LANGUAGE.md), [projects](PROJECTS.md) |
| Format and debug code | [CodeStyle](CODESTYLE.md), [debugging](DEBUGGING.md) |
| Work with text and collections | [Text](TEXT.md), [buffers](BUFFERS.md), [results](RESULT.md) |
| Inspect typed expressions | [Quotations](QUOTATIONS.md) |
| Accept typed field arguments | [Fields](FIELDS.md) |
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
