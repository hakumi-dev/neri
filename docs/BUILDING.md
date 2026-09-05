# Building Neri

Use this guide to build the compiler from a source checkout. To write programs
with an installed toolchain, start with the [installation instructions](../README.md#install).

## Requirements

The bootstrap host is macOS on Apple silicon. Install the Xcode Command Line Tools
and these dependencies:

- LLVM 22.1.8 and zstd.
- CMake 3.28 or newer and Ninja 1.11 or newer.
- `curl` for downloading the verified bootstrap seed.

```sh
brew install llvm@22 zstd cmake ninja
```

Run the following commands from the repository root.

## Build and test

```sh
scripts/build.sh doctor
scripts/build.sh test
```

The launcher verifies a prebuilt seed compiler and uses it to compile the Neri
build driver. The driver builds the native components, compiles two generations
of the compiler and requires matching output. It then runs the language and
native tests before selecting the verified toolchain at `build/current`.
See [bootstrapping](BOOTSTRAP.md) and [testing](../tests/README.md) for details.

Use that toolchain without changing your installed `neri` command:

```sh
scripts/neri.sh examples/hello.hk
scripts/neri.sh build examples/functions.hk --release
./functions
```

## Build native components

```sh
scripts/build.sh native
scripts/build.sh native --debug
```

These commands build the C++ code generator and runtime. CMake and LLVM are build
dependencies for this native boundary. Linux x86-64 native validation is described
in [the testing guide](../tests/README.md#linux-native-validation).

## Package and install

```sh
scripts/build.sh package
scripts/build.sh install
```

Packaging runs the full validation suite and produces two byte-identical archives.
Verified packages are stored in `build/packages/` with their SHA-256 in the filename.
Installation builds and validates a package, then installs it under `~/.neri` by
default. `scripts/build.sh install --prefix /your/directory` selects another prefix.
See [packaging and installation](PACKAGING.md) for integrity checks and PATH setup.
