# Reference assemblies

Neri reference assemblies let a consumer compile against a library's supported
declarations while using the library's separately built native object at link
time. The reference file has the `.nref` extension. It contains typed metadata
and the digest and path of the corresponding object; it is not executable and
does not contain the implementation.

## Build and consume

Emit a reference assembly from a library unit into an existing output directory:

```sh
neri build --project library/manifest.json --unit library --emit=reference --output build/library.nref
```

Pass one or more references when building, running, or checking a consumer:

```sh
neri build --project app/manifest.json --unit app --reference build/library.nref
neri run --project app/manifest.json --unit app --reference build/library.nref
neri check --project app/manifest.json --unit app --reference build/library.nref
```

The compiler writes `library.nref` and `library.nref.o`. The reference records
the object's absolute path. Keep both at their emitted locations; rebuild the
reference after moving the object. The consumer project must not also include
the provider through a source-based project reference.

The consumer reads declarations from the reference metadata and uses its
associated object digest and path for native linking. The consumer can be
compiled from the reference metadata without the library source files. The
object must be available and match the recorded digest on every import,
including `check`. Compatibility is deliberately conservative: compiler binary,
code generator, runtime manifest, runtime archive, and target must match.
File identities are SHA-256 hashes of contents. Inputs to the bounded binary
reader must be regular files no larger than 128 MiB.

## Version 1 contract

Version 1 supports module functions with `Void` results and `Bool`, `Byte`,
`Int`, `Float`, or `String` parameters/results. Module functions have Neri's
ordinary module visibility; v1 exports all functions owned by the library unit.
Its metadata
describes the declarations needed to resolve calls and check types. The
interface hash is computed from this typed contract, so changing an ordinary
function body leaves the interface hash unchanged. The implementation hash
covers the native object and changes when that object changes. A changed
interface requires consumers to be checked against the new declarations; a
changed implementation with the same interface does not change the consumer's
type-checking contract.

This is a compatibility boundary, not an automatic incremental build cache.
Binary references do not retain consumer dependency fingerprints or reuse prior
consumer analysis when an interface hash is unchanged. Builds that consume a
reference still perform the ordinary consumer compilation. Executable and
partitioned object caches are bypassed for binary-reference consumers until
their dependency keys represent the complete reference graph.

Classes, generic declarations, default arguments, reference and quote types,
and ABI exports are outside the version 1 contract. The compiler diagnoses
these unsupported declarations when emitting a reference assembly rather than
silently omitting them. Generic support needs body metadata because a consumer
may specialize a generic template and compile its body.

Provider bodies may call other supported functions in the same unit and the
Neri runtime. Dependencies on Neri functions or classes outside the unit, native
libraries, and external C ABI imports are diagnosed. Chained binary reference
emission and session modules are also outside v1. Runtime requirements travel
with the implementation metadata, separately from the interface digest.
Imported calls receive conservative effect summaries.

The compiler stages both outputs and publishes metadata last. Import validates
metadata and object fingerprints; an interrupted replacement produces a
diagnostic rather than silently accepting inconsistent files. Concurrent writes
to the same reference output are unsupported.

This subset establishes separate compilation. Sumi and Neri Data still require
class layout, generics, transitive reference graphs, manifest integration, and
dependency-based incremental reuse before they can use it throughout their
dependency trees. Project manifests and the LSP do not resolve `.nref` files.
This reference format does not provide incremental compilation for `sumi s`.

## Validation

`reference-assembly-contracts` exercises source-independent check/build/run,
integer and string calls, multiple libraries, duplicate declarations, body-only
and signature changes, missing or corrupted objects, incompatible metadata, and
explicit rejection of unsupported exports and body dependencies. This contract
has macOS arm64 coverage with LLVM 22.1.8; coverage on other native targets is
not established here. Performance measurements follow the
[benchmark result policy](../benchmarks/README.md#source-and-result-policy).

## Design basis

The separation between declarations and implementations follows the reference
assembly model documented by Microsoft: compiler inputs need the observable
API surface, while implementation bodies can change independently. Neri's
typed metadata applies that idea to its current scalar type and module
function subset.

The hash boundary also follows established incremental compilation and build
system work. GHC records fingerprints for interface declarations and for the
things a module used, rather than relying only on a dependency's source-file
hash. *Build Systems à la Carte* describes early cutoff: when a rebuilt
dependency's observable result is unchanged, downstream work can stop at that
boundary. Neri's current reference workflow defines separate interface and
implementation hashes without downstream analysis reuse.

References:

- Microsoft, [Reference assemblies](https://learn.microsoft.com/en-us/dotnet/standard/assembly/reference-assemblies).
- Andrey Mokhov, Neil Mitchell, and Simon Peyton Jones, [Build Systems à la Carte: Theory and Practice](https://simon.peytonjones.org/assets/pdfs/build-systems-jfp.pdf), especially §§2.3 and 5.2.
- GHC User's Guide, [The recompilation checker](https://ghc.gitlab.haskell.org/ghc/doc/users_guide/separate_compilation.html#the-recompilation-checker).
