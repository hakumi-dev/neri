# Persistent checked analysis

Neri stores checked compiler data between processes in a private, versioned cache.
The first layer retains a whole eligible analysis context. It supports classes,
contracts, generic template syntax, instantiated generic bodies, aliases, native
record metadata and bound expressions. Normal object construction recreates the
arenas and lookup indexes. The payload contains explicit values and indexes;
object addresses and native executable memory are never serialized.

In memory, child semantic models share indexed sequences of imported names,
generic template IDs and declared namespaces. A fork shares a 32-way tree root
in constant time; appending copies only shared nodes along its path and mutates
exclusive leaves. Parent, child and sibling sequences preserve independent
contents. Indexed reads traverse the tree instead of walking the session's chain
of parents. The snapshot format continues to encode each sequence as count and
items. The wide-tree representation follows the immutable-vector structure
described by Bagwell and Rompf in
[RRB-Trees: Efficient Immutable Vectors](https://infoscience.epfl.ch/record/169879/files/RMTrees.pdf);
Neri's append-only sequence does not implement RRB concatenation or splitting.

## Consumers and responsibility

- Session initializer analysis reuses the exact project and loaded standard-library
  snapshot. It still generates the session frame, lowers and verifies IR, loads
  native code and executes initialization on every opening. Database connections,
  query results and other application state belong to that execution.
- Ordinary executable project compilation and batch compilation reuse an eligible
  dependency prefix before analyzing the consumer. The exact executable artifact
  lookup takes precedence. Ordinary builds group all units in the selected root
  manifest with the consumer; external manifests and standard-library sources can
  form the dependency prefix. Batch builds also retain eligible sibling libraries
  to share analysis across executable units. Consumer body changes can reuse the
  dependency model.
- Sumi consumes the compiler and session SDK; it owns startup presentation.

Dependency sources must precede consumer sources in the compiler's sorted source
order. Consumer declarations that can affect dependency name resolution,
consumer alias imports and interleaved ownership select normal whole-program
analysis. Import identity comes from typed `SyntaxUse` declarations. Source-symbol models
used by editor analysis and models with parent arenas are outside this format.

## Identity and publication

A cache entry identifies the format version, compiler executable digest, runtime
manifest contents, cache purpose and length-framed analysis inputs. Session inputs
include configuration, ordered source identities and contents, imports, and loaded
library identities and contents. Dependency inputs include ordered source IDs,
ownership, contents, boundaries and effective consumer imports.

Entries live under `$NERI_CACHE_DIR/semantic`, or `$HOME/.neri-run-cache/semantic`.
Both directories must be owned by the current user and have mode 0700. A checksum
covers the payload. Reads check the file's ownership, kind and identity before and
after reading. Publication writes a private staging file and atomically renames it.
Missing, unreadable, oversized, incompatible or checksum-invalid entries select
normal analysis. The cache is private compiler output, not a public interchange
format for untrusted semantic models. The checksum detects corruption; it is not
an authenticity signature against another process running as the same user.

The decoder bounds counts, string lengths and node references, reconstructs domain
values by name, validates declaration construction invariants and requires exact
end-of-payload. Payloads are limited to 64 MiB. Strings use a per-payload intern table.

`NERI_SEMANTIC_CACHE=0` disables persistent checked analysis. `--no-cache` also
bypasses persistence in project builds; batch compilation can still share an
eligible analysis in memory. `--timings` reports `dependency-semantic-hit` or
`dependency-semantic-miss`. `NERI_SEMANTIC_METRICS=/absolute/path.jsonl` records session
initializer analysis cache status and duration. These durations include lookup and,
on a miss, analysis and publication; they are not complete startup times.

The scalar `.nref` reference artifact format remains separate. This internal cache
is tied to the exact compiler rather than a portable binary API contract.

## Research basis and limits

- Mokhov, Mitchell and Peyton Jones, [Build Systems a la Carte (ICFP 2018)](https://www.microsoft.com/en-us/research/publication/build-systems-la-carte/),
  supplies the dependency and rebuild model: all inputs that affect a result must
  participate in its reuse decision.
- Smits, Konat and Visser, [Constructing Hybrid Incremental Compilers for Cross-Module
  Extensibility with an Internal Build System (2020)](https://programming-journal.org/2020/4/16/),
  motivates staged reuse of existing compiler components with mixed granularity.
- The [rustc incremental compilation guide](https://rustc-dev-guide.rust-lang.org/queries/incremental-compilation.html)
  describes persisted query dependencies, selective result serialization and
  red-green early cutoff. Neri currently reuses a coarse exact context; it does not
  implement rustc's fine-grained query graph or semantic early cutoff.

These sources guide the design; correctness and speed for Neri require Neri's own
restoration, invalidation and workload measurements. Cache hits preserve the same
type and resource checks by reusing only a successful analysis of identical inputs.
