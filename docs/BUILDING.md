# Building Neri

Use this guide to build the compiler from a source checkout. To write programs
with an installed toolchain, start with the [installation instructions](../README.md#install).

## Requirements

On macOS Apple silicon, install the Xcode Command Line
Tools and these dependencies:

- LLVM 22.1.8 and zstd.
- CMake 3.28 or newer and Ninja 1.11 or newer.
- `curl` for downloading the verified bootstrap seed.

```sh
brew install llvm@22 zstd cmake ninja
```

For native Windows x86-64 builds, use the [Windows build guide](WINDOWS.md).
It covers the PowerShell 7 entry point, Visual Studio C++ and Windows SDK
requirements, the pinned LLVM and GitHub Actions seed downloads, and the
Windows CMake presets.

Clone the source from GitHub, then run the following commands from its root:

```sh
git clone https://github.com/hakumi-dev/neri.git
cd neri
```

For Linux dependencies and full source installation, follow
[Linux setup from GitHub](LINUX.md).

## Build and test

The root `manifest.json` defines the compiler and tooling units. The `build` and
`install` executables reference the shared `tooling` library, which references
process support. Library directories discover new `.hk` files automatically;
entry-point files belong to explicit executable units. To check tooling:

```sh
neri check --project manifest.json --unit build
neri check --project manifest.json --unit install
```

The trusted bootstrap seed predates manifests. `scripts/build.sh` enumerates
the bootstrap sources for the seed; the current compiler uses the root
manifest for package installation and seed transport generation.

```sh
scripts/build.sh doctor
scripts/build.sh test
```

`scripts/build.sh test` builds and validates the current checkout, then selects
its verified toolchain at `build/current`. See [bootstrapping](BOOTSTRAP.md) for
generation checks and [testing](../tests/README.md) for suite composition.

The opt-in `scripts/build.sh debugger-test` command verifies real LLDB debugging
on macOS; see [debugging](DEBUGGING.md) for setup and the supported contract.

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
compiler. Semantic editor support for `.hk` files is provided by the
[Neri language server](LANGUAGE-SERVER.md); C++ completion is independent of it.
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

See [packaging and installation](PACKAGING.md) for verified archives, installer
options, integrity checks and PATH setup.
