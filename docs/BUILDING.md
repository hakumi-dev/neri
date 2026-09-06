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

Clone the source from GitHub, then run the following commands from its root:

```sh
git clone https://github.com/hakumi-dev/neri.git
cd neri
```

For Linux dependencies and full source installation, follow
[Linux setup from GitHub](LINUX.md).

## Build and test

```sh
scripts/build.sh doctor
scripts/build.sh test
```

The launcher verifies a prebuilt seed compiler and uses it to compile the Neri
build driver. The driver builds the native components, compiles three generations
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

### Linux editor setup

Open `CMakeLists.txt` in a CMake-capable editor (including Rider with CMake
support). Select `linux-debug` for development or `linux-release` for optimized
native builds. Reload the CMake project after changing presets. These profiles
use the Debian/Ubuntu LLVM 22 installation at `/usr/lib/llvm-22` and generate
`compile_commands.json` in `build/native/<preset>/` for C++ tooling.
They clear inherited pkg-config and library search paths to avoid accidentally
linking another project's environment. Other LLVM layouts can override
`LLVM_DIR` and compiler paths with a local CMake user preset.

```sh
cmake --preset linux-debug
cmake --build --preset linux-debug --parallel 4
ctest --preset linux-debug
```

The same commands accept `linux-release`, `linux-sanitize` (ASan/UBSan), and
`linux-thread-sanitize` (TSan). Run sanitizer configurations separately.
These profiles cover the C/C++ backend and runtime, not the self-hosted `.hk`
compiler. There is currently no Neri language server or editor grammar in this
repository; C++ completion does not provide semantic completion for `.hk` files.
The `.editorconfig` uses two-space indentation, UTF-8 and LF line endings.
See [Linux setup from GitHub](LINUX.md) for dependencies and the remaining
installation instructions.

### macOS bootstrap host

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
