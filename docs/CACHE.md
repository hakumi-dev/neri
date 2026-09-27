# Compiler cache contracts

Caches contain disposable compiler output. Neri accepts only entries whose
producer, inputs and integrity checks match the current operation. Unsupported,
missing, incompatible or damaged entries select normal compilation. Old internal
formats have no compatibility readers or migration path.

## Identity and responsibility

`CacheDomain` identifies an operation. `cacheIdentity` hashes the domain, format,
producer and ordered inputs, each framed with its byte length. The constructor is
pure: it does not inspect the host or discover dependencies. Each cache supplies
the observations required by its computation.

| Layer | Producer and inputs |
| --- | --- |
| Semantic snapshot | Compiler bytes, runtime manifest and ordered analysis context. The payload format is bound to the same compiler. |
| Reference artifact | Compiler, codegen, runtime manifest and runtime bytes, plus declared contracts and object contents. |
| Dependency library | Compiler and runtime manifest through the semantic identity, codegen closure, dependency sources and imports, target, optimization and debug identity. Declaration images use the same enclosing identity. |
| Project executable | Compiler bytes, source contents and ownership, manifests, configuration and the native link inputs. |
| AOT object | Codegen identity, target, optimization, unit configuration and the complete IR envelope identity. Identical emitted IR does not acquire a dependency on unrelated frontend changes. |
| Session object/executable | Backend-specific domain, serialized IR, session configuration, compiler and retained-module dependencies. |
| Native link | Compiler link-policy identity, resolved driver/linker/LTO contents, loader observations, actual linker inputs and missing search candidates. |
| Native codegen object | Loaded codegen image closure, target, optimization and serialized input. The closure includes the producing executable. |

The IR envelope identity is computed from the same header used by serialization,
including its transport version, length and payload digest. Domain labels identify
purpose; they are not manually incremented cache generations. Producer hashes
invalidate implementation changes where the producer is part of the contract.

`CacheRecordReader` and `CacheRecordWriter` implement the bounded record codec.
Semantic reconstruction stays in the semantic snapshot codec. Receipts and
snapshots do not need each other's semantic reconstruction rules.
The shared record policy limits payloads to 64 MiB and individual inventories to
one million records. Checksummed storage adds a 64-character SHA-256 line.

Retained compilation outputs also serve the seed publisher. Their manifest uses
a shared wire-format definition consumed by both compiler and build tooling.
This interchange contract is separate from cache identity: cache reuse still
checks its enclosing producer/input key and the retained file digests.

## Files and limits

File content identities use standard SHA-256 with a 1 MiB read buffer and an
explicit maximum file size of 64 GiB. The buffer size does not limit the file size.
The reader checks EOF, size and descriptor identity around the read and reports
read, metadata, mutation, hashing and cleanup failures separately. A digest is
not returned after a cleanup failure. These observations detect ordinary
concurrent modifications; they are not a filesystem snapshot or protection
against another process with the same user's privileges.

The runtime supplies incremental SHA-256 and descriptor metadata primitives in
ABI 1.34. Neri owns file reads, limits, comparison and cleanup policy. The stream
context must be finished at most once and disposed exactly once after creation.

`NativeLinkLimits` bounds loader observation separately: paths and short queries
use 4096 bytes, traces and native receipts use 4 MiB, dependency inventories use
4096 entries, and tool observations have a 10-second deadline. Mach-O validation
has its own bounded command/table parsing and total-I/O policy. A limit rejection
disables reuse; it does not make an otherwise compilable program invalid.

## Storage and outcomes

Artifact generations live under `artifacts/<key>/generation.<id>` in the selected
private cache root. Publication makes a complete generation visible atomically.
Readers verify ownership, permissions, receipts and observed dependencies.
Previously missing native search candidates are dependencies too: a new file can
change which library the loader selects.

`RunCache.outcome` distinguishes an ordinary miss from unsupported configurations,
unavailable dependencies, changed inputs, incompatible formats, invalid entries
and storage failures. With `--timings`, run and AOT lookups report exceptional
fallback reasons on stderr. Cache policy does not depend on Sumi's presentation;
Sumi consumes the compiler/session behavior.

Executable reuse remains restricted to the supported macOS configuration and
known native dependency closure. This contract does not imply identical cache
coverage across macOS, Linux and Windows or a fine-grained incremental query DAG.

## Design references

- Mokhov, Mitchell and Peyton Jones, [Build Systems à la Carte](https://www.microsoft.com/en-us/research/publication/build-systems-la-carte/): explicit dependencies and sound rebuild decisions.
- [rustc incremental compilation](https://rustc-dev-guide.rust-lang.org/queries/incremental-compilation-in-detail.html): stable identities, dependency tracking and avoiding unnecessary invalidation.
- [NIST FIPS 180-4](https://csrc.nist.gov/pubs/fips/180-4/upd1/final): the standard SHA-256 digest used for file contents.
