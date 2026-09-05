# Neri

Neri is a statically typed programming language that compiles to native executables.
It combines local type inference, closures and automatic memory management with
explicit access to low-level operations.

- Explicit optional values and checked integer arithmetic and array indexing.
- Functions, typed arrays and classes with single inheritance.
- Automatic memory management for objects, strings, arrays and closures.
- `unsafe` blocks for raw memory operations and C interoperability.

Programs are written in `.hk` files.

## Install

On Apple silicon with macOS 15 or newer:

```sh
brew install hakumi-dev/tap/neri
neri --version
```

Homebrew installs LLVM and zstd. The Xcode Command Line Tools are also required.
For other installation methods, see [standalone installation](docs/PACKAGING.md#standalone-installer)
or [building from source](docs/BUILDING.md).

Neri is in development. This repository documents its source revision; packaged
releases include the language reference for their compiler.

## Write a program

Save this as `main.hk`:

```neri
use ConsoleInteraction

def main(): Void
  ConsoleInteraction.println("Hello, Neri!")
end
```

Run it, check it without running, or build an executable:

```sh
neri main.hk
neri check main.hk
neri build main.hk
./main
```

Use `neri build main.hk --release` for optimized output. The default is Debug.

## Documentation

- [Examples](examples/README.md) — output, functions, arrays and program arguments.
- [Language reference](docs/LANGUAGE.md) — syntax, types and command-line behavior.
- [Documentation index](docs/README.md) — installation, development and internals.

## Development

See [building Neri](docs/BUILDING.md) to work on the compiler and
[testing Neri](tests/README.md) to verify changes. Report bugs and propose changes
in the [issue tracker](https://github.com/hakumi-dev/neri/issues), including a small
source example and the expected behavior where applicable.
