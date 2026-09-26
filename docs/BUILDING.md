# Building Neri

Source-build requirements and command contracts. Installed-toolchain usage is
documented in [installation](../README.md#install). Commands use the repository root.

## Supported platforms

| Target | Native ABI and development path |
| --- | --- |
| `macos-arm64` | Apple silicon macOS; bootstrap and package workflow below |
| `linux-x86_64` | Native Linux with Clang/LLVM 22.1.8; [Linux setup](LINUX.md) |
| `windows-x86_64` | Native Windows x64 with Win32 APIs and the MSVC ABI; [Windows setup](WINDOWS.md) |

Windows support targets native execution. WSL and MinGW are outside the supported
setup. POSIX worker and PTY helpers require platform-specific replacements;
Windows coverage uses its own native and language contract suite.

CI runs macOS and Linux jobs and Windows Debug and Release jobs. The required
`Required / supported platforms` check combines all three platform results;
branch protection for `main` enforces it on remote pull requests.

## Requirements

On macOS Apple silicon, install the Xcode Command Line
Tools and these dependencies:

- LLVM 22.1.8 and zstd.
- CMake 3.28 or newer and Ninja 1.11 or newer.
- `gzip` for unpacking the verified compiler IR seed included in the repository.

```sh
brew install llvm@22 zstd cmake ninja
```

For native Windows x86-64 builds, use the [Windows build guide](WINDOWS.md).
It covers the PowerShell 7 entry point, Visual Studio C++ and Windows SDK
requirements, the pinned LLVM download, and the
Windows CMake presets.

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

The native backend materializes the checked-in IR seed as a host compiler.
That compiler builds the Neri build driver through the root manifest. Every
compiler generation uses the current compiler unit and standard library.

| Command | Contract |
| --- | --- |
| `scripts/build.sh doctor` | Check native build prerequisites. |
| `scripts/build.sh test` | Build and validate the checkout; select the verified toolchain at `build/current`. |
| `scripts/build.sh debugger-test` | Opt-in LLDB contract on macOS; see [debugging](DEBUGGING.md). |
| `scripts/neri.sh <arguments>` | Use the selected checkout toolchain without changing the installed compiler. |

`scripts/build.sh test` builds and validates the current checkout, then selects
its verified toolchain at `build/current`. See [bootstrapping](BOOTSTRAP.md) for
generation checks and [testing](../tests/README.md) for suite composition.

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

## Compiler progress for packaged frontends

The compiler SDK's `compiler-core` unit exposes `bootstrapCompile(arguments,
 batch, progress, nativeProgress)`. The last three parameters are optional;
command-line compilation uses the same implementation with no callbacks.
The call retains the CLI's exit-status behavior, including executing the program
for `run`, and is intended for dedicated compiler frontend processes.

`progress` accepts `(phase: String, projectSources: Int, librarySources: Int)`.
Its phases describe project loading, analysis, compilation, cache lookup/reuse,
lowering, native generation, linking, and the boundary before application output
(`load`). Counts are provided for `analyze` and `compile`. `nativeProgress`
accepts `(phase: String, completed: Int, total: Int, detail: String)` and reports
native function lowering, verification, optimization, emission, and `end`.
Lowering detail includes the source filename when available. Counts describe the
current native module; partitioned compilation can report several modules.

Frontends own terminal rendering and should clear transient output at `load`,
`link`, and native `end`. Omitting callbacks leaves compilation and caching
unchanged. Progress events describe actual work and do not imply that console JIT
modules and ahead-of-time executables share an artifact cache.
