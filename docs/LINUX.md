# Linux source builds

Neri targets Linux x86-64 with glibc. The native backend and runtime materialize
the hash-verified compiler IR seed, then compile the current sources. Commands
use the repository root; installation is source-based.

## Requirements

| Requirement | Configuration |
| --- | --- |
| Platform | Debian/Ubuntu x86-64 with glibc; CI uses Ubuntu 24.04. Linux ARM64 and musl are outside this configuration. |
| Compiler backend | Clang/LLVM **22.1.8**, under `/usr/lib/llvm-22` for bundled presets. |
| Build tools | Git, CMake ≥3.28, Ninja ≥1.11, gzip and a C++ toolchain. |
| Native development libraries | zlib, zstd, libedit, libffi, libxml2, OpenSSL and SQLite; `libarchive-tools` for package handling. |

Debian/Ubuntu package command:

```sh
sudo apt-get install -y git ca-certificates curl gnupg lsb-release \
  software-properties-common cmake ninja-build build-essential \
  zlib1g-dev libzstd-dev libedit-dev libffi-dev libxml2-dev libssl-dev \
  libsqlite3-dev libarchive-tools gzip clang-22 llvm-22-dev
```

The pinned LLVM repository configuration is in the [Linux CI job](../.github/workflows/ci.yml).
It requires administrator access. Repository packages can advance beyond the
required patch version; Neri's version check remains mandatory. Version probes
are `cmake --version`, `ninja --version`, `clang-22 --version` and
`llvm-config-22 --version`.

## Get the source

Source: [hakumi-dev/neri](https://github.com/hakumi-dev/neri).
`git rev-parse HEAD` identifies the revision associated with measurements.

## Install the compiler

| Command | Contract |
| --- | --- |
| `scripts/build.sh install` | Verify seed hashes, bootstrap the compiler, run native/language contracts, check packaging and install under `~/.neri`. |
| `scripts/build.sh install --prefix /your/directory` | Use an explicit installation root. |
| `scripts/neri.sh <arguments>` | Use the checkout's verified compiler. |
| `export PATH="$HOME/.neri/bin:$PATH"` | Expose the default installed launcher in the current shell. |

Builds require a clean native-library environment. The explicit invocation is:

```sh
env -u PKG_CONFIG_PATH -u CPPFLAGS -u LDFLAGS -u LIBRARY_PATH -u LD_LIBRARY_PATH \
  scripts/build.sh install
```

The installer retains previous toolchains. Installing an updated source revision
builds and validates its candidate before activation.

## Build native components separately

| Preset | Scope |
| --- | --- |
| `linux-release` | Optimized native backend and runtime. |
| `linux-debug` | Native development and debugging. |
| `linux-sanitize` | ASan/UBSan in a separate build directory. |
| `linux-thread-sanitize` | TSan in a separate build directory. |

For each preset, use `cmake --preset <preset>`,
`cmake --build --preset <preset> --parallel 4` and `ctest --preset <preset>`.
`cmake --fresh --preset <preset>` discards a previous CMake configuration.

Outputs under `build/native/<preset>/` include `neri-codegen`,
`libneri-runtime.a`, the runtime manifest and native helpers. These commands do
not install the compiler or bootstrap the self-hosted `.hk` sources.
`neri-codegen` consumes compiler IR, not `.hk` source.

See [editor configuration](BUILDING.md#linux-editor-setup) and
[native test scope](../tests/README.md#linux-native-validation).

## Performance

The [benchmark reference](../benchmarks/README.md) defines workloads and result
metadata. Release measurements apply to their recorded host, target and revision;
VM results do not characterize a different workstation.
