# Local dependency libraries

Project executable builds can compile an eligible dependency closure into a
separate native object. Subsequent application builds load its checked semantic
snapshot and ABI declarations, lower the application's definitions, and link the
existing dependency object. This reuse applies across executable units and output
paths with the same ordered dependency closure and import context.

The provider owns its ordinary definitions and the concrete generic instances
required by those definitions. The consumer reuses those instances and owns new
specializations requested by application code. Generic templates retain their
checked syntax and compile-time bodies. ABI declarations preserve class layout,
inheritance, fields, method dispatch, native records, function effects, and native
imports. Declaration symbols are remapped to the consumer IR module; native
symbol names remain qualified semantic names with ABI types.

## Local artifact contract

The private semantic cache stores a versioned `.nlib` declaration image beside
its native `.o` implementation. Its identity includes the complete ordered
dependency sources, source identities and ownership, import context, compiler,
runtime manifest, code generator, target, optimization mode, and debug source
paths. It excludes the consumer's body, executable unit name, and output path.
The checked semantic snapshot uses the same dependency input contract.

Readers verify bounded metadata, its digest, the object digest, file ownership,
regular-file status, and stable file identity during reading. They validate
decoded declarations with the IR verifier. Writers stage files privately and
publish metadata last. The linker checks the native object digest again before
using it. Missing or corrupt artifacts are rebuilt. Native compilation or
declaration validation failures fail the build rather than silently succeeding
through another compilation path.

All transitive native imports and runtime requirements accompany the provider,
including libraries used only by implementation bodies. Declaration images also
record implementation symbol edges. Executable linking follows those edges from
the entry point and explicit C exports to select native libraries. The normal
linker resolves and fingerprints the selected external link inputs.
See [Executable reachability](EXECUTABLE-REACHABILITY.md) for entry roots, native
section collection, interactive hosts, and the current-schema-only policy.

The first eligible build creates the local library. A fresh application output
can reuse it without dependency lowering or native code generation. An exact
application cache hit continues to bypass that work altogether. `--no-cache`
uses source compilation. `NERI_LIBRARY_CACHE=0` disables native dependency reuse;
`NERI_SEMANTIC_CACHE=0` disables both semantic and native dependency persistence.
Timing output distinguishes `dependency-native-hit`, `dependency-native-miss`,
`dependency-lower`, and `dependency-codegen` from application phases.

## Current boundaries

This is a compiler-owned local artifact format. It is not a portable package
reference format or an ABI stability promise across toolchain versions. Current
reuse requires a closed dependency prefix in deterministic source order. Alias
imports, possible consumer-to-dependency name-resolution changes, sessions, and
ineligible source graphs retain the existing compilation path.

An implementation change invalidates the whole dependency closure. Independent
per-package images, public API-only consumer invalidation, and shipping prepared
libraries with a toolchain or framework package require additional contracts.
The scalar `.nref` format retains its existing separate contract.

## Local validation

On macOS arm64, the installed Sumi frontend built the SQLite demo in 14.59 s
including first-time dependency analysis and native compilation. A fresh
application output with those libraries prepared took 3.11 s; its application
lowering took 111 ms and native code generation 419 ms. An exact warm build took
0.43 s. These are local wall-clock samples of the build path, excluding Sumi's
package verification and HTTP startup, rather than a portable performance claim.

The prior frontend took 5.35 s for a fresh output with its semantic and object
caches already prepared. The new path avoids lowering, verifying, partitioning,
and generating dependency bodies again. Initial dependency preparation remains
necessary until libraries can be supplied as compatible package artifacts.

Focused validation covers declaration roundtrips, inheritance and dispatch,
consumer generic functions and instance/static methods, shared reuse across
executable units, debug/release separation, corrupt metadata recovery, dependency
body invalidation, and source compilation with caching disabled. Existing
semantic snapshot, project manifest, and incremental build contracts pass. The
installed Sumi CRUD and console queries use temporary SQLite databases.

## Design references

- [Microsoft: Reference assemblies](https://learn.microsoft.com/en-us/dotnet/standard/assembly/reference-assemblies)
  motivates separating compilation metadata from executable implementation.
  Neri's native generics additionally require template bodies.
- [rustc development guide: Monomorphization](https://rustc-dev-guide.rust-lang.org/backend/monomorph.html)
  describes the distinction between dependency definitions and consumer generic
  instantiations. Neri retains already-created provider instances as well.
- [Mokhov, Mitchell and Peyton Jones, Build Systems à la Carte, ICFP 2018](https://www.microsoft.com/en-us/research/publication/build-systems-la-carte/)
  informs complete dependency tracking and rebuilding contracts.
- [Smits, Konat and Visser, Constructing Hybrid Incremental Compilers for Cross-Module Extensibility with an Internal Build System, 2020](https://programming-journal.org/2020/4/16/)
  provides context for combining persistent semantic work with separately reused
  native compilation. Neri's implementation is verified by its own contracts.
