# Bootstrapping Neri

## Trust root

The pinned `v0.2.0-dev` release is the bootstrap input for `macos-arm64`.
Its provenance records fixed-point and native/language verification and the
provenance digest of its predecessor, `bootstrap-seed-v1`. The archive and
extracted compiler/native artifacts are independently pinned in this repository.

The seed contains:

- a Neri-native compiler executable;
- the C++ `neri-codegen` executable;
- the C++ Neri runtime archive and manifest.

The compiler is invoked directly at `libexec/neri`; the package's user-facing
`bin/neri` wrapper is not used for bootstrap. The extracted cache directory
includes the archive digest so an earlier seed cache remains untouched.

`SOURCE-MANIFEST.sha256` identifies the compiler, native implementation, native
test inputs, CMake configuration, and version used to produce and verify the seed.

`bootstrap/macos-arm64.seed` pins the release, asset name, and archive SHA-256. `scripts/fetch-bootstrap-seed.sh` rejects any mismatch before extracting or executing the seed. `bootstrap/macos-arm64.files.sha256` pins the extracted tools, runtime manifest, and provenance; these hashes are checked on every cached-seed launch.

## Fixed point

`scripts/build.sh bootstrap` compiles the Neri driver with the trusted seed.
The driver builds the native backend and runtime from this checkout, then performs
three generations in an isolated `build/work.*` directory:

1. the trusted seed compiles `compiler/` into Stage1;
2. Stage1 compiles the same sources into Stage2;
3. Stage2 compiles the same sources into Stage3;
4. Stage1 and Stage2 must emit byte-identical canonical NIR and objects, and
   Stage2 and Stage3 executables must match byte for byte.

The seed establishes Stage1; it may use an older internal IR naming convention.
Fixed-point checks compare generations running the current compiler sources,
without normalizing or ignoring any output differences.

Compiler, tooling and user sources use `console`. The pinned release already
supports this spelling, so no legacy import alias or source rewriting is needed.

Every compiler process receives the current native artifact paths, the pinned LLVM linker, the macOS SDK path, a C locale, UTC, and the host `PATH`. Any unexplained difference fails the bootstrap. Native configuration independently checks Clang/LLVM and Ninja versions.

Native builds require an explicit CMake build type. Seed preparation and the
Neri driver use the same absolute compiler paths under `LLVM_PREFIX`, preserving
the compiler identity and Release configuration across native build invocations.

The pinned compiler uses the `0.2.0-dev` toolchain label. Seed, Stage1 and Stage2
calls all receive the current native runtime manifest; artifact compatibility
checks and fixed-point comparisons remain enabled. The initial build-driver
compilation uses the pinned package's own codegen and runtime.

Both generations use the executable basename `neri` in separate directories.
The macOS linker embeds that basename in its ad-hoc signing identifier, so the
basename is part of the reproducibility contract.

The verified Stage3 compiler, codegen, runtime and manifest are copied together
under `build/toolchains/<artifact-manifest-sha256>`. An atomic symlink replacement
selects that immutable tuple at `build/current`. `build/neri` is a compatibility
link to its compiler. Failures preserve the last published tuple; native build
directories are development outputs and are not used by an already published
toolchain. Work directories retain comparisons and test output for inspection.

`scripts/build.sh test` runs native probes and language contracts against the current
native artifacts and candidate compiler, then publishes the tuple only after
those tests pass. `scripts/neri.sh` resolves `build/current` once and uses the
compiler, codegen and runtime from that immutable directory for the whole invocation.

## Provenance

- Release: `v0.2.0-dev`
- Target: `macos-arm64`
- Compiler implementation: Neri
- Runtime ABI: `1.8`
- Neri IR transport: `1.1`

The archive's `PROVENANCE.json` records its source-manifest digest and artifact hashes. The archive SHA-256 is pinned in `bootstrap/macos-arm64.seed`.

Linux x86-64 uses the candidate pinned in `bootstrap/linux-x86_64.seed` and its
extracted-file manifest. The pin identifies GitHub Actions run `34046783143`
and the archive digest. It bootstraps locally using `bin/neri` and the
target-specific runtime manifest; GitHub CLI access is needed to download it.

## Preparing a Linux seed

The manual `Linux bootstrap seed candidate` workflow produces a `linux-x86_64`
candidate on Ubuntu 24.04 with Clang/LLVM 22.1.8. It has read-only repository
permissions and publishes workflow artifacts, not a release or a new trust pin.

`scripts/build.sh export-seed` runs the current compiler contracts and exports
canonical IR for the compiler and Neri build driver. The transport includes the
source commit, source inventory, frontend artifact hashes and transport hashes.
The Linux job checks those identities before generating native entry tools.

The native driver performs the fixed-point and language/native contract checks,
compares its compiler IR with the transported IR, and creates two byte-identical
normalized seed archives. `tooling/seed.hk` owns this preparation logic. Archive
normalization uses `bsdtar` from `libarchive-tools` on Linux.

The candidate contains the native compiler, code generator, runtime archive and
manifest, source inventory and provenance. `build/seeds/` also contains candidate
archive and extracted-file pins. A final job step extracts the candidate, checks
its file pins and bootstraps through `scripts/build.sh` on Linux. Publishing a
release seed remains a separate publication step. The current Linux pin downloads
the verified Actions candidate instead; its availability depends on artifact retention.

Linux source launchers use `/usr/lib/llvm-22` by default; `LLVM_PREFIX` selects
another installation. The run cache currently supports macOS ARM64. Linux runs
compile and execute without persistent executable-cache reuse.
