# Neri

Neri is a statically typed programming language that compiles to native executables.
It combines automatic memory management with explicit access to low-level operations.

- Static types with local type inference and explicit optional values.
- Functions, typed arrays, and classes with single inheritance.
- Automatic memory management for objects, strings, and arrays.
- Checked integer arithmetic and array indexing.
- Explicit `unsafe` blocks for raw memory operations and C interoperability.

Programs are written in `.hk` files. Run one with `neri main.hk`, or build an
executable with `neri build main.hk`.

## Start here

### 1. Install Neri

The current Homebrew distribution supports macOS 15 or newer on Apple silicon:

```sh
brew install hakumi-dev/tap/neri
neri --version
```

Homebrew installs LLVM and zstd and manages the `neri` command. The Xcode Command
Line Tools are required. See the [Homebrew tap](https://github.com/hakumi-dev/homebrew-tap)
for package requirements and updates.

For a standalone installation, install LLVM 22.1.8 with `brew install llvm@22`,
download and extract an [installable release](https://github.com/hakumi-dev/neri/releases/tag/v0.1.0-dev),
and run its `install.sh`:

```bash
./install.sh
export PATH="$HOME/.neri/bin:$PATH"
neri --version
```

`install.sh --prefix /your/directory` selects another installation root.
Installation requires no `sudo` and leaves shell configuration unchanged. Add the
PATH line to your shell configuration to keep it across sessions. The package is
self-contained apart from LLVM and the Xcode command-line tools; using it requires
neither CMake nor the repository. `LLVM_PREFIX` selects an explicit LLVM installation.

To create a package from a source checkout, follow [Build and install from source](#build-and-install-from-source).

### 2. Write your first program

Create `main.hk`:

```neri
use ConsoleInteraction

def main(): Void
  ConsoleInteraction.println("Hello, Neri!")
end
```

Run it:

```sh
neri main.hk
# Hello, Neri!
```

### 3. Build an executable

```sh
neri build main.hk
./main
# Hello, Neri!
```

`neri main.hk` is shorthand for `neri run main.hk`. Native output defaults to
the installed runtime target; `--target` remains available for explicit object
emission. `neri build main.hk` writes `./main`, with `--output` selecting another
path. `neri check main.hk` validates without running. `--release` enables optimized
output; the default is Debug.

### 4. Explore the language

Follow the [three runnable examples](examples/README.md) for output, functions
and arrays, and command-line arguments. They are included in toolchain packages.
The [language reference](docs/LANGUAGE.md) describes the current syntax and contracts.

## Build and install from source

Building Neri from source starts with a verified, prebuilt compiler. The newly
built compiler then rebuilds itself, and both resulting executables must match
byte for byte. See the [bootstrap contract](docs/BOOTSTRAP.md) for details.

Requirements:

- macOS on Apple silicon
- Homebrew LLVM 22.1.8 and `zstd`
- CMake 3.28 or newer and Ninja 1.11 or newer
- `curl` for downloading the verified bootstrap seed

Build and verify the compiler:

```bash
brew install llvm@22 zstd cmake ninja
scripts/build.sh doctor
scripts/build.sh test
scripts/build.sh install
```

Compile or run a Neri program with the self-hosted compiler:

```bash
scripts/neri.sh build program.hk --release
scripts/neri.sh program.hk
```

Build the C++ codegen and runtime from source:

```bash
scripts/build.sh native
scripts/build.sh native --debug
```

Create and validate a reproducible local toolchain package:

```bash
scripts/build.sh package
```

The driver runs the contracts, creates two byte-identical archives, checks the
extracted artifact hashes and compiles a program with the extracted launcher.
Verified archives live under `build/packages/` with their SHA-256 in the filename.
See [the package contract](docs/PACKAGING.md).

## Repository layout

- `examples/` contains small runnable programs and a getting-started guide.
- `compiler/` contains the Neri compiler.
- `native/codegen/` contains the LLVM code generator.
- `native/runtime/` contains the runtime ABI implementation.
- `tooling/` contains the Neri build and validation driver.
- `scripts/` contains seed and driver launchers.

See [the language](docs/LANGUAGE.md), [the executable contracts](tests/README.md), [the roadmap](ROADMAP.md), [the bootstrap contract](docs/BOOTSTRAP.md), and [the architecture](docs/ARCHITECTURE.md).
