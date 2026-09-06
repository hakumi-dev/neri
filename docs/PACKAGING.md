# Toolchain packages

`scripts/build.sh package` produces a Release toolchain archive for the bootstrap
host. The command builds the current native components, reaches the compiler
fixed point, and passes the native and language contracts before packaging.

The archive contains a `bin/neri` launcher, native codegen, compiler executable,
an `install.sh` launcher and Neri-native installer with its filesystem helper,
runtime archive and manifest, language/ABI/performance documentation, runnable
examples with a getting-started guide in `examples/README.md`, and three
records:

- `SOURCE-MANIFEST.sha256` hashes source, build, test and documentation inputs using
  repository-relative names, excluding Finder's `.DS_Store` metadata and generated
  `bin/` and `obj/` outputs of the C# benchmark reference. Changes detected during validation or packaging fail
  the command.
- `ARTIFACTS.sha256` hashes packaged executables, libraries, documentation, examples and the
  source manifest.
- `PROVENANCE.json` records the target, LLVM version, ABI and IR versions, validation
  gate, source/artifact manifest digests and trusted seed provenance digest.

The driver creates two separate trees, normalizes permissions and timestamps,
writes sorted USTAR members with fixed owner/group metadata, and compresses with
gzip's filename/timestamp metadata disabled. The archives must match byte for byte.
The extracted artifact manifest is verified. Its installer installs into an isolated
prefix containing spaces, and the installed launcher must compile and run the smoke
program from the extracted examples without an explicit subcommand or target.
The installed launcher also builds its default executable and runs the documented
arguments example. The archive is then renamed onto the same build filesystem
under `build/packages/`, with its content SHA-256 in the filename.

The launcher requires LLVM 22.1.8 and the platform SDK/linker environment. On macOS,
it locates Homebrew LLVM and the active Xcode SDK; `LLVM_PREFIX` can select an
explicit LLVM installation. Source compilation uses the codegen/runtime inside the
package. Program commands and arguments follow the [language tooling contract](LANGUAGE.md).

Packaging operates locally and performs no release upload or remote publication.
Two archives from the same verified binaries prove packaging reproducibility;
comparison across independent source checkouts additionally tests native-build and
compiler reproducibility across build locations.

## Installation

`brew install hakumi-dev/tap/neri` installs the versioned distribution through the
[Neri Homebrew tap](https://github.com/hakumi-dev/homebrew-tap). The formula pins
the release asset and SHA-256, declares LLVM and zstd dependencies, and preserves
the verified package under its `libexec` directory in the Homebrew Cellar.
Its `bin/neri` wrapper selects the declared LLVM installation. Homebrew manages
updates and removal; this path does not invoke the standalone installer.

Homebrew release artifacts come from a passing package CI run. The formula's
minimum macOS version matches the distributed binaries; the current package
supports Apple silicon on macOS 15 or newer with LLVM 22.1.8.

### Standalone installer

Install LLVM 22.1.8 and zstd, download an
[installable release](https://github.com/hakumi-dev/neri/releases/tag/v0.2.0-dev),
and extract its archive. On macOS, the Xcode Command Line Tools provide the SDK.
From the extracted directory:

```sh
./install.sh
export PATH="$HOME/.neri/bin:$PATH"
neri --version
```

Add the PATH setting to your shell configuration to keep it across sessions.

`install.sh [--prefix <directory>]` installs under `~/.neri` by default.
`scripts/build.sh install [--prefix <directory>]` builds and verifies a package,
then invokes the same installer. Installation policy lives in `tooling/install.hk`;
the shell launcher locates the package and starts that binary.

The installer verifies artifact hashes before and after copying, stores the package
under `toolchains/<artifact-manifest-sha256>`, and atomically replaces `bin/neri`
with a relative symlink. Existing versions remain available. Reinstallation verifies
the existing version; unrelated executables and symlinks are preserved by refusing
to replace them. The launcher resolves its symlink before locating its native tools.
The install prefix's `bin` directory belongs in `PATH`; shell configuration is
managed by the user. Removing that symlink disables the installation, and individual
unused toolchain directories can be removed independently.
