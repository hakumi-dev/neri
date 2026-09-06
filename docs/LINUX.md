# Build from GitHub on Linux

This guide builds, tests and installs Neri from source on Linux x86-64.
Bootstrap uses the hash-pinned `linux-seed-candidate` artifact from GitHub Actions
run `34046783143`, built from commit `561344b201a9c3801a76f311328bfaaad06d4bf2`.
It is not yet a published release asset. GitHub CLI (`gh`) access to the artifact
is required for the first download; Actions artifacts expire, so this candidate
is a temporary installation source. Distribution packages are deferred.

## Requirements

The commands below target Debian/Ubuntu with glibc. The native CI uses Ubuntu
24.04; local Debug, Release and ASan/UBSan validation has also passed on Debian
13 x86-64. Other distributions, Linux ARM64 and musl are not covered here.

Install Git, CMake 3.28 or newer, Ninja 1.11 or newer, Clang/LLVM **22.1.8**,
and the native development dependencies:

```sh
sudo apt-get update
sudo apt-get install -y git ca-certificates curl gnupg lsb-release \
  software-properties-common cmake ninja-build build-essential \
  zlib1g-dev libzstd-dev libedit-dev libffi-dev libxml2-dev libarchive-tools gh
```

Clang/LLVM 22 must be available from your configured package repositories before
running the next command. If it is absent, follow the pinned LLVM repository
setup in [the Linux CI job](../.github/workflows/ci.yml). That setup changes apt
repositories and requires administrator access. Package repositories may advance
beyond the required patch version; do not bypass Neri's version check.

```sh
sudo apt-get install -y clang-22 llvm-22-dev
cmake --version
ninja --version
clang-22 --version
llvm-config-22 --version
```

Check the versions before continuing. The Linux presets expect LLVM under
`/usr/lib/llvm-22`. If the distribution's CMake is too old, install CMake 3.28
or newer first.

## Get the source

```sh
git clone https://github.com/hakumi-dev/neri.git
cd neri
git rev-parse HEAD
```

Keep the printed commit with test or benchmark results. Run the remaining
commands from this directory. This guide describes the revision containing it;
older tags may not include the Linux presets.

## Install the compiler

Authenticate GitHub CLI if needed, then build and install:

```sh
gh auth status
# If not authenticated:
gh auth login

env -u PKG_CONFIG_PATH -u CPPFLAGS -u LDFLAGS -u LIBRARY_PATH -u LD_LIBRARY_PATH \
  scripts/build.sh install
export PATH="$HOME/.neri/bin:$PATH"
neri --version
neri examples/hello.hk
neri check examples/generics.hk
neri build examples/functions.hk --release --output build/functions
./build/functions
```

The clean environment prevents inherited library paths from another project from
affecting the build. If a stale `GITHUB_TOKEN` overrides working GitHub CLI
credentials, remove that variable from this invocation as well.

Installation verifies the archive and extracted seed hashes, bootstraps the
compiler, runs native and language contracts, checks reproducible packaging and
installs under `~/.neri`. Add its `bin` directory to your shell's PATH permanently,
or create a symlink from an existing PATH directory. A custom location can be
selected with `scripts/build.sh install --prefix /your/directory`.

Expected smoke output is `Hello, Neri!` and `Total: 60`. The source checkout's
verified compiler is also available through `scripts/neri.sh`. Re-run the install
command after updating the sources; the installer preserves previous toolchains.

## Build native components separately

```sh
cmake --preset linux-release
cmake --build --preset linux-release --parallel 4
ctest --preset linux-release
./build/native/linux-release/neri-codegen --help
```

The build produces `neri-codegen`, `libneri-runtime.a`, the runtime manifest,
native helpers and tests under `build/native/linux-release/`. All five native
contracts should pass. `neri-codegen` consumes compiler IR; it cannot compile
`.hk` source by itself. These artifacts stay in the checkout and do not add a
`neri` command to your PATH.

For development and memory-error checks, use separate build directories:

```sh
cmake --preset linux-debug
cmake --build --preset linux-debug --parallel 4
ctest --preset linux-debug

cmake --preset linux-sanitize
cmake --build --preset linux-sanitize --parallel 4
ctest --preset linux-sanitize
```

See [editor setup](BUILDING.md#linux-editor-setup) for Rider/CMake and
[testing](../tests/README.md#linux-native-validation) for the native test scope.
If a build directory was configured with another compiler or inherited library
paths, reconfigure it with `cmake --fresh --preset linux-release` before rebuilding.

## Performance

The installed Linux compiler can now build the benchmark workloads directly.
Follow the [benchmark guide](../benchmarks/README.md) and use this host's Release
builds for local observations. Existing VM results do not measure this workstation.
