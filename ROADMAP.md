# Roadmap

Neri evolves through two ordered delivery stages. The compiler remains self-hosted, and the C++/LLVM code generator remains the specialized native backend boundary.

## Current system

- The frontend, semantic analysis, Neri IR, lowering, and compiler driver are implemented in Neri.
- `bootstrap-seed-v1` produces consecutive byte-identical compiler generations.
- `neri-codegen` consumes verified Neri IR and emits native objects through LLVM.
- The native runtime provides ABI 1.6 and a precise tracing collector.
- The Neri build driver orchestrates native builds, fixed-point bootstrap and executable contract tests. Shell launches the seed and driver; CMake builds native targets.

## Stage 1: Neri build system

Tracking issue: [#1 — Implement the build system in Neri](https://github.com/hakumi-dev/neri/issues/1)

Outcome:

- A build driver implemented in Neri owns bootstrap, builds, checks, tests, packages, provenance, and CI orchestration.
- The trusted seed compiles the build driver before it builds the compiler.
- Local and CI workflows use the same Neri commands and deterministic artifact layout.
- Shell is limited to a portable launcher that fetches or locates the seed and starts the build driver.
- CMake and LLVM remain declared tools invoked by the Neri driver for the native backend.

Completion gate:

- A clean checkout reaches the compiler fixed point through the Neri build driver.
- Debug and Release builds, the complete test corpus, package reproducibility, and live progress reporting are preserved.
- Build policy no longer lives in shell scripts.

## Stage 2: Neri runtime and garbage collector

Tracking issue: [#2 — Implement the runtime and garbage collector in Neri](https://github.com/hakumi-dev/neri/issues/2)

Dependency: Stage 1 must be complete.

Outcome:

- Neri implements runtime initialization, managed objects, roots, allocation, tracing, collection, borrows, strings, arrays, class objects, panic behavior, and standard process I/O.
- Exported runtime symbols continue to implement the negotiated Neri ABI.
- Platform-specific allocation, entry, I/O, and assembly remain behind a minimal documented shim.
- The LLVM backend, assembler, and linker remain external native boundaries.

Completion gate:

- ABI probes and the complete language corpus pass on macOS ARM64 and Linux x86-64.
- Collector stress, sanitizer-compatible shims, bootstrap workloads, and memory/performance budgets pass.
- The self-hosted compiler links and reaches its reproducible fixed point with the Neri runtime by default.

## Scope discipline

- New language capabilities must be required by the build driver or runtime implementation and include a focused contract.
- Backend replacement, direct machine-code emission, a Neri assembler, and a Neri linker are outside these stages.
- Kanban status and ordering are authoritative for delivery; this document defines the current technical direction and completion gates.
