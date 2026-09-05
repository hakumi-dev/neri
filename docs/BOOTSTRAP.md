# Bootstrapping Neri

## Trust root

The `bootstrap-seed-v1` release is the trust root for `macos-arm64`. Its provenance records the compiler/native source inventory, target, ABI, IR transport version, fixed-point verification, and hashes of every executable or archive.

The seed contains:

- a Neri-native compiler executable;
- the C++ `neri-codegen` executable;
- the C++ Neri runtime archive and manifest.

`SOURCE-MANIFEST.sha256` identifies the compiler, native implementation, native
test inputs, CMake configuration, and version used to produce and verify the seed.

`bootstrap/macos-arm64.seed` pins the release, asset name, and archive SHA-256. `scripts/fetch-bootstrap-seed.sh` rejects any mismatch before extracting or executing the seed. `bootstrap/macos-arm64.files.sha256` pins the extracted tools, runtime manifest, and provenance; these hashes are checked on every cached-seed launch.

## Fixed point

`scripts/build.sh bootstrap` compiles the Neri driver with the trusted seed.
The driver builds the native backend and runtime from this checkout, then performs
two generations in an isolated `build/work.*` directory:

1. the trusted seed compiles `compiler/` into Stage1;
2. Stage1 compiles the same sources into Stage2;
3. Stage1 and Stage2 canonical NIR, objects, and executables are compared byte for byte.

Seed-compiled compiler and tooling sources use the seed-compatible
`ConsoleInteraction` spelling. User programs use `console`; both resolve to the
same runtime operations and preserve the canonical import identity.

Every compiler process receives the current native artifact paths, the pinned LLVM linker, the macOS SDK path, a C locale, UTC, and the host `PATH`. Any unexplained difference fails the bootstrap. Native configuration independently checks Clang/LLVM and Ninja versions.

The pinned seed requires the `0.1.0-dev` toolchain label in its runtime manifest.
Native builds generate a seed-only compatibility manifest with that label and
the current artifact, ABI and feature values. Only trusted-seed compiler calls
receive this manifest. Stage1, Stage2 and distributed packages receive the current
versioned manifest; artifact compatibility checks and fixed-point comparisons
remain enabled. The compatibility manifest is not distributed.

Both generations use the executable basename `neri` in separate directories.
The macOS linker embeds that basename in its ad-hoc signing identifier, so the
basename is part of the reproducibility contract.

The verified Stage2 compiler, codegen, runtime and manifest are copied together
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

- Release: `bootstrap-seed-v1`
- Target: `macos-arm64`
- Compiler implementation: Neri
- Runtime ABI: `1.6`
- Neri IR transport: `1.1`

The archive's `PROVENANCE.json` records its source-manifest digest and artifact hashes. The archive SHA-256 is pinned in `bootstrap/macos-arm64.seed`.

Linux x86-64 remains a supported native target, but it requires a separately built and verified Linux seed before it can be advertised as a bootstrap host.
