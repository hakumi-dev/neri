# Bootstrapping Neri

This page defines the seed trust boundary, compiler fixed point and seed-refresh
contract. For dependency installation, use [Building](BUILDING.md) or
[Windows](WINDOWS.md). Run the commands below from the repository root.

| Task | Entry point | Result |
| --- | --- | --- |
| Build native components without a seed | `scripts/build-native.sh` | Native backend, runtime and host helper |
| Bootstrap on macOS or Linux | `scripts/build.sh bootstrap` | Stage1 compiler tuple selected by `build/current` |
| Verify compiler self-reproduction | `scripts/build.sh bootstrap --stage 3` | Stage2 and Stage3 IR, object and executable equality |
| Build a distribution | `scripts/build.sh package` | Stage2 compiler with native, language and installation contracts |
| Validate before publishing a compiler tuple | `scripts/build.sh test` | Native and language contracts pass before publication |
| Regenerate the canonical seed | `scripts/build.sh refresh-seed` | Validated seed candidate and provenance replace the previous set |
| Build and test on Windows | `scripts/build.ps1 -Action test` | Stage1 compiler and Windows platform contracts |

Compiler build commands require the complete seed artifact set below. Native-only
compilation does not establish compiler or seed validation.

## Required seed artifacts

Bootstrapping requires these four files together. The launcher fails when a
file is absent or a digest does not match.

| File under `bootstrap/` | Contract |
| --- | --- |
| `compiler.nir.gz` | Canonical binary Neri IR, compressed with `gzip -n` |
| `SOURCE-MANIFEST.sha256` | Exact compiler, standard-library, manifest and template inputs |
| `VALIDATION-SOURCE-MANIFEST.sha256` | Broader source snapshot used by the validation run |
| `seed.json` | Artifact, IR and manifest hashes; producer identity, arguments and validation record |

[`scripts/build.sh`](../scripts/build.sh) and
[`scripts/build.ps1`](../scripts/build.ps1) require these files and verify their
hashes. They do not fetch an alternate compiler when a file is missing.

## Trust root

The seed format is canonical binary compiler IR in `bootstrap/compiler.nir.gz`, compressed
with gzip's filename and timestamp metadata disabled. The same seed is used on
macOS ARM64, Linux x86-64 and Windows x86-64. The host's C++ toolchain builds
Neri's native backend and runtime, which materialize this IR as the Stage0
compiler.

The `bootstrap/seed.json` schema records SHA-256 digests for the compressed artifact, its
uncompressed IR and `bootstrap/SOURCE-MANIFEST.sha256`. That source manifest
records repository-relative paths and exact content hashes for the seed's
inputs. Generated seed artifacts are excluded from their own source inventory.
The provenance identifies the producing compiler and compilation arguments.
`bootstrap/VALIDATION-SOURCE-MANIFEST.sha256` identifies the broader source tree
used for verification, including native code, tooling and tests. Its digest is
also recorded in the seed metadata. It records the bootstrap inputs present
during that verification run.

The launcher verifies the seed's artifact, IR and source-manifest digests before
executing it. Ordinary builds can compile changed sources using the fixed seed;
the current checkout need not match the seed's source inventory. Refreshing the
seed verifies source identity throughout generation and validation.

The seed IR, native backend, runtime and host toolchain form the trust
root. Content hashes detect mismatched artifacts; fixed-point checks establish
reproducibility for the checked sources and toolchain.

## Toolchain version transitions

The compiler checks the runtime's exact toolchain version in addition to its
IR transport, target, ABI version and required features. A canonical seed with
the previous version pin cannot compile against a runtime bearing a new version.

Before changing `VERSION`, generate a validated bridge seed whose runtime check
accepts exactly the previous and next versions. Keep the current version in
`VERSION` and the CLI during that refresh. Then update those values and restore
the compiler's strict check to the new version, and run `refresh-seed` again.
Both refreshes use the ordinary fixed-point and contract gates. The final
package gate materializes the final seed and validates the delivered toolchain;
run that gate instead of adding another standalone full test invocation.

Keep the ABI, feature, target and IR checks in place throughout this transition.
Seed artifacts and their provenance are published by the refresh command.

## Compiler stages

Stage0 is the pinned seed materialized with the current native backend. Stage1
is the current compiler built by Stage0 and is the default for development and
language contracts. Stage2 is built by Stage1 and is the default for `package`
and `install`. Stage3 is built by Stage2 and compares self-reproduction against
Stage2. The supported-platform CI requests Stage3 explicitly.

`--stage 1|2|3` selects the final generation and composes with build-mode options;
`install` also accepts `--prefix <directory>`. Packages require Stage2 or Stage3.
`refresh-seed` requires Stage3. Windows uses the corresponding `-Stage` option,
with Stage1 for `build` and `test`, and Stage2 for `install`.

The Unix flow is implemented by [the launcher](../scripts/build.sh) and
[`Build.bootstrap`](../tooling/build.hk):

```mermaid
flowchart TD
  Native["Current C++ backend and runtime"] --> Materialize["Verify and materialize canonical seed IR"]
  Seed["Seed artifact set"] --> Materialize
  Materialize --> S0["Stage0 compiler"]
  S0 --> Driver["Compile current Neri build driver"]
  Driver --> Stages["Orchestrate compiler generations"]
  S0 -->|"Current compiler sources"| S1["Stage1"]
  S1 -->|"Same sources"| S2["Stage2"]
  S2 -->|"Same sources"| S3["Stage3"]
  Stages -.-> S1
  S1 --> Development["Select development toolchain"]
  S2 --> Package["Validate and assemble distribution"]
  S3 --> Verify["Compare IR, objects and executables"]
  Verify --> Publish["Publish only after required checks pass"]
```

`scripts/build.sh bootstrap` builds the native components, materializes Stage0
and uses it to compile the Neri build driver through the root `manifest.json`.
The driver builds only the requested generations in an isolated `build/work.*`
directory:

1. Stage0 compiles the current compiler unit into Stage1.
2. Stage1 compiles the same sources into Stage2.
3. Stage2 compiles the same sources into Stage3.
4. When Stage3 is requested, Stage2 and Stage3 must contain byte-identical
   serialized NIR envelopes, objects and executables.

Successful commands remove their invocation's workspace after publishing the
selected toolchain or package. Failed commands retain their workspace for
diagnostics. `benchmark` retains its measurements and prints their location.
Other invocations' workspaces are never removed automatically.

Each requested generation performs one compiler invocation. On Unix, Stage2 and
Stage3 retain the object and NIR transport from that executable compilation
using `--save-temps`; comparison does not repeat frontend or native generation.
Retained files live in `OUTPUT.temps`, with `unit-<index>.o`, its NIR transport,
and a manifest of unit identities and SHA-256 digests. This includes every
partition actually compiled and linked. The executable cache validates and
restores the complete retained set when that option is requested.
Windows emits NIR once per generation and materializes that same NIR as COFF
and PE through the native backend.

Every generation uses the current project manifest, compiler sources and
standard library. The NIR comparison uses the envelope's hexadecimal encoding;
objects and executables are compared directly. Each compiler
process receives the current native artifacts, LLVM linker, platform settings,
C locale, UTC and host `PATH`. Native configuration checks the supported
Clang/LLVM and Ninja versions and requires an explicit CMake build type.

The compiler executable basename is `neri` in each generation's directory.
The macOS linker embeds that basename in its ad-hoc signing identifier, making
it part of the reproducibility contract.

The selected compiler, codegen, runtime and manifest are copied together
under `build/toolchains/<artifact-manifest-sha256>`. An atomic symlink replacement
selects that immutable tuple at `build/current`. Failures preserve the last
published tuple and retain comparisons and diagnostics in their work directory.

Build commands use a private compiler cache under `build/cache/compiler`.
Producer copies under `build/cache/producers` have content-addressed identities.
Cache lookup still resolves the current source closure and validates native
dependencies; an existing executable alone is insufficient. Stage messages
distinguish execution from reuse and include elapsed milliseconds.

On macOS and Linux, Stage0 object generation uses the private cache at
`build/cache/bootstrap-objects`. Its identity includes the decoded seed, target,
optimization mode and the native generator's loaded executable and libraries.
macOS shared-cache images use the loader's shared-cache UUID; other images are
verified against their mapped files and hashed. Entries contain a checked object
and receipt, with publication serialized and atomic. Incomplete dependency
discovery, unsafe loader environments and unsafe cache paths use normal code
generation. The backend reports `hit`, `miss` or `unavailable`. Stage0 linking
still runs with the current runtime and linker.

This native cache operates before a Neri executable is available. Compiler
generation, contract selection and package policy remain in the Neri driver.
The macOS image identity comes from the SDK's `TASK_DYLD_INFO` and
[`dyld_all_image_infos`](https://github.com/apple-oss-distributions/dyld/blob/main/include/mach-o/dyld_images.h).

`scripts/build.sh test` runs native probes and language contracts against the
current native artifacts and candidate compiler, then publishes the tuple after
those tests pass. `scripts/neri.sh` resolves `build/current` once and uses its
compiler, codegen and runtime with the standard-library sources from the same
checkout. Packaged launchers use their toolchain's bundled standard library.

Windows uses `scripts/build.ps1` to materialize the same seed, compile the
current compiler unit and run its platform contracts. `-Stage 3` additionally
compares NIR, COFF and PE output. See [Windows](WINDOWS.md) for dependencies and commands.

## Refreshing the seed

This command requires a working canonical seed; it cannot create the first seed
from native components alone.

```sh
scripts/build.sh refresh-seed
```

The Neri implementation in [`tooling/seed.hk`](../tooling/seed.hk) runs the native and language
contracts and fixed-point checks, emits canonical compiler IR, verifies its
source inventory and checks deterministic compression. It records the exact
source contents and producing compiler rather than treating a Git commit as
proof of the working tree's contents.

`seedSourceManifest` covers the compiler and standard-library source files,
their manifests and template assets. `Build.sourceManifest` captures the broader
validation inputs. Refresh compares both inventories before generation and
publication. `seedPublish` serializes publication with `build/seed-refresh.lock`,
backs up the previous files and attempts rollback if a replacement fails.

Review the refreshed artifact, source manifest and provenance together. The
repository's supported-platform CI jobs are configured to materialize the seed
on each host and validate current sources using that host's native toolchain.
Refresh prepares the candidate in an isolated work directory and promotes its
metadata last. Launchers reject mismatched artifact sets during publication;
the repository files are not a single atomic filesystem transaction.

Linux launchers use `/usr/lib/llvm-22` by default; `LLVM_PREFIX` selects another
installation. The persistent executable run cache supports macOS ARM64. Linux
and Windows compile and execute without persistent executable-cache reuse.

## Implementation and evidence

| Contract | Source |
| --- | --- |
| Native tool versions, target selection and runtime manifest | [CMakeLists.txt](../CMakeLists.txt), [native launcher](../scripts/build-native.sh) |
| Unix seed hash verification and Stage0 materialization | [scripts/build.sh](../scripts/build.sh) |
| Generations, comparisons and compiler publication | [tooling/build.hk](../tooling/build.hk) |
| Seed inventories, compression and publication | [tooling/seed.hk](../tooling/seed.hk) |
| Windows materialization, comparisons and source stability | [scripts/build.ps1](../scripts/build.ps1) |

These links identify implementation checks. Passing a fixed-point comparison
does not establish independent compiler correctness or cross-platform test
completion; those claims require the corresponding validation output.

## References

- [Rust compiler development guide: compiler stages](https://rustc-dev-guide.rust-lang.org/building/bootstrapping/what-bootstrapping-does.html)
  separates the downloaded seed, the development compiler and the distribution
  compiler. Neri applies this distinction to its verified IR seed and stages.
- [GCC build procedure](https://gcc.gnu.org/install/build.html) describes the
  Stage2/Stage3 comparison used to check compiler self-reproduction.
- [Mokhov, Mitchell and Peyton Jones, *Build Systems à la Carte* (2018)](https://www.microsoft.com/en-us/research/wp-content/uploads/2018/03/build-systems.pdf)
  separates dependency scheduling from deciding which results require rebuilding.
  Neri stage selection controls scheduling; the compiler cache validates inputs
  before deciding whether an executable can be reused.
- [Zig's portable bootstrap seed](https://ziglang.org/news/goodbye-cpp/) describes
  a checked-in WebAssembly compiler seed materialized using the system toolchain.
  Neri applies that portable-artifact approach using its own canonical IR and
  native backend.
- [Wheeler, *Fully Countering Trusting Trust through Diverse Double-Compiling*](https://dwheeler.com/trusting-trust/dissertation/)
  distinguishes self-regeneration from independent source-to-binary assurance.
  Neri's fixed-point gate verifies self-regeneration; diverse double-compilation
  requires an independently trusted compiler.
