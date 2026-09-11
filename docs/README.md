# Neri documentation

These documents describe the source revision that contains them. Toolchain
packages include the language reference for the packaged compiler.

## Use Neri

- [Install and run a program](../README.md#install).
- [Work through the examples](../examples/README.md).
- [Look up syntax, types and commands](LANGUAGE.md).
- [Configure code style and automatic corrections](CODESTYLE.md).
- [Debug with LLDB and IDEs](DEBUGGING.md).
- [Serve HTTP requests](HTTP.md).
- [Read files and manage temporary filesystem state](FILES.md).
- [Use elapsed time and UTC timestamps](CLOCK.md).
- [Build bounded byte, text and ordered-value buffers](BUFFERS.md).
- [Use UTF-8 text and validated Unicode scalars](TEXT.md).
- [Hash bytes and obtain operating-system entropy](CRYPTO.md).
- [Handle typed results and owned failures](RESULT.md).
- [Build interactive terminal applications](TERMINAL.md).
- [Install a standalone toolchain](PACKAGING.md#standalone-installer).
- [Supported platforms](PLATFORMS.md) — native targets and platform limits.

## Develop Neri

- [Building](BUILDING.md) — tools, source builds and local installation.
- [Linux setup from GitHub](LINUX.md) — dependencies, verified bootstrap, tests
  and local compiler installation.
- [Windows setup](WINDOWS.md) — native x64 prerequisites, bootstrap, tests and
  local compiler installation.
- [Testing](../tests/README.md) — language contracts and native validation.
- [Architecture](ARCHITECTURE.md) — compiler, native components and build driver.
- [Compiler boundaries](COMPILER-BOUNDARIES.md) — source APIs, contracts and native operations.
- [Language server](LANGUAGE-SERVER.md) — editor-independent live diagnostics and current limits.
- [Compilation projects](PROJECTS.md) — compilation units, automatic source discovery and explicit library references.
- [Bootstrapping](BOOTSTRAP.md) — trusted seed and reproducible compiler generations.
- [Packaging](PACKAGING.md) — distribution contents, integrity and installation.
- [Runtime and IR](ABI.md) — layouts, memory management and native boundaries.
- [Performance](PERFORMANCE.md) — workloads, measurements and regression budgets.

Bugs and proposals are tracked in [GitHub issues](https://github.com/hakumi-dev/neri/issues).
