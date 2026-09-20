# Memory and performance checks

This document describes workloads, measurement boundaries and enforced contracts.
Store and publish per-run evidence according to the
[benchmark source and result policy](../benchmarks/README.md#source-and-result-policy).

## UTF-8 construction

`benchmarks/text.hk` compares the `String` concatenation operator, the `text::concat` library
wrapper and `buffers::TextBuffer` with a reserved final capacity. Each appends
`abcdé😀` (10 bytes, six scalars) and checks the resulting byte/scalar lengths.
Build with `neri build benchmarks/text.hk --release --output build/text-benchmark`;
run `/usr/bin/time -l build/text-benchmark <concat|library|builder> <count>` on macOS.

Milliseconds measure the construction region inside the executable;
RSS covers the entire process, including runtime and final validation.
The class uses the native string allocation directly. Its methods add no wrapper
object or separate backing array. The buffer path reuses
capacity and copies bytes directly into its array through Neri code.

For `N` appends of `k` bytes to a flat immutable string, the total output bytes
copied are `k Σ(i=1..N) i = kN(N+1)/2`, hence quadratic in `N`. Reserved buffer
construction plus the final snapshot costs `O(kN)` with `O(kN)` storage.
This arithmetic describes copy work; GC thresholds, allocator behavior and
cache effects determine observed RSS and time. The representation tradeoffs are
discussed in [Boehm, Atkinson and Plass, Ropes (1995)](https://www.cs.tufts.edu/comp/150FP/archive/hans-boehm/ropes.pdf).
Neri retains flat immutable strings and supplies a separate mutable builder.

## Run latency

`neri profile --project ROOT` measures release builds of all declared project
members and referenced libraries. Its structured output records per-phase
duration totals and sample counts, binary artifact digests, input fingerprints
and project snapshots before and after the run. Libraries are measured as object
builds. The profiler builds executables without running them.

`NERI_TIMING_OUTPUT` selects the JSONL timing sink when `--timings` is enabled.
The terminal `build-complete` event establishes that all successful build phases
were recorded; a write failure fails the profiling build. The sink is bounded
to 8 MiB. Repeated phases, such as object code generation, are grouped in the
project response. Nested durations overlap; `build-complete` supplies the total.
Measurements are individual observations with the current cache state, not
statistical regressions or application profiles.

`neri source.hk --timings` separates frontend, cache lookup, code generation,
linking and program execution. `--no-cache` provides an uncached comparison with
the same compiler, runtime and safety checks. A cache hit still parses, type-checks,
lowers and verifies the program; it skips native code generation and linking.

Frontend timings distinguish `source-load`, `parse`, `bind`, `lower` and
`ir-verify`. They use the same JSONL sink consumed by project profiling. The
`frontend` phase measures their enclosing compilation interval; phase durations
overlap and must not be added to their enclosing total.

## Batch builds

`neri build-batch --project manifest.json --output-dir build/contracts
--unit first --unit second --timings` builds the selected executable units in
one process. The output directory exists before invocation. Each unit has its
own executable, entry point, consumer declarations and generic specializations.
All unit selections are validated before compilation starts. A failed build
returns a nonzero status; artifacts from preceding successful units remain.

The batch retains one dependency analysis in memory. Its identity includes exact
source contents, ordered source identities and the effective import context.
Each unit loads its current sources before reuse is considered. An eligible
dependency prefix is parsed and bound once; child syntax and bound arenas retain
that analysis while binding each consumer separately. IR lowering, verification,
partitioning and native artifact checks run for every executable.

Sharing applies when dependencies precede consumer sources in the compiler's
source order and resolve independently. Consumer declarations that could change
dependency name resolution, interleaved ownership and dependency diagnostics
select ordinary whole-program analysis. Changes to dependency contents or import
resolution establish a new retained analysis. `--no-cache` disables native object
reuse; the batch still shares eligible in-memory semantic analysis.

Timings report `dependency-parse` and `dependency-bind` when establishing an
analysis. The final batch summary reports analyzed and reused dependency contexts
and units that selected whole-program analysis. The compiler tooling contract
stage uses batch builds and executes each resulting contract with its own process
and deadline.

Dependency-aware reuse follows the model described in the
[Rust compiler's incremental compilation guide](https://rustc-dev-guide.rust-lang.org/queries/incremental-compilation.html).
Neri's batch cache is process-local and retains a whole eligible dependency
context; it does not persist a fine-grained semantic query graph.

## Compiler data structures and native caching

Deterministic IR lowering and transport use one stable, typed merge sort to order
compiler collections. Ordering takes `O(N log N)` comparisons and preserves
insertion order for equal keys, following the standard
[merge sort](https://www.nist.gov/dads/HTML/mergesort.html) and
[merge](https://www.nist.gov/dads/HTML/merge.html) definitions.

Syntax and bound arenas retain original node objects in linked lists. A binary
index stores a checkpoint for every 128 nodes. Adjacent accesses use the cached
cursor in `O(1)` time; other accesses take `O(log(1 + N / 128) + 128)` steps for
`N` local nodes. Index storage is `O(1 + N / 128)`, and parent arenas retain their
own indexes. Appending a checkpoint grows the binary index by doubling its
capacity when full, following the standard
[doubling and amortized-analysis technique](https://ocw.mit.edu/courses/6-046j-introduction-to-algorithms-sma-5503-fall-2005/resources/lecture-13-amortized-algorithms-table-doubling-potential-method/).

Transport bytes occupy linked 256-byte arrays, with `ceil(N / 256)` chunks
and less than 256 bytes of unused tail capacity for `N` bytes. Each buffer
constructs one zero template; copying that template creates independent chunks.
Sequential writes and checksum input traverse the chunks in `O(N)` time;
reads before the current chunk restart from the first chunk. Hexadecimal
encoding writes two ASCII digits per byte into packed chunks, converts each
chunk to a string, then joins pairs in balanced rounds. This uses `O(N)` storage
and `O(N log(1 + N / 256))` character-copy work, without allocating a string for
each digit. These bounds describe transport storage and encoding, not the
complete compiler heap or compilation time; the IR remains live during encoding.

The key hashes the canonical IR transport and a length-delimited build context
with SHA-256. The context includes target, optimization mode, runtime manifest,
working directory, selected SDK/developer tools, deployment target and PATH.
File identities include device/inode, permissions, size and nanosecond mtime/ctime
for codegen, the linker, runtime archive and selected platform link inputs.
Access time is excluded. This cache assumes installed LLVM and SDK distributions
and their transitive native dependencies are immutable; use `--no-cache` when
developing or modifying these dependencies. External `@library` dependencies and custom search/injection
environments are uncached. Other host ABIs use the uncached path.

Each entry contains the executable and its file-identity receipt, under a
user-owned 0700 directory. A mismatch is a miss. Builds happen in private staging
directories, recheck dependency identities, and publish by atomic directory rename.
Concurrent builders may duplicate work; readers only accept complete entries.
Arguments and runtime environment are applied on every execution. Program output
and exit status are never memoized. Cache I/O failures preserve uncached execution.

Run the standalone Neri benchmark from the repository root:

```sh
scripts/neri.sh build benchmarks/run-latency.hk compiler/ir/process.hk --release --output build/run-latency
./build/run-latency scripts/neri.sh examples/hello.hk build/run-latency.jsonl
```

It creates an isolated empty cache, records its first run, warms both cases,
then records ten alternating-order samples each for warm-cache and uncached runs.
The monotonic wall measurements include the launcher and child program, with
millisecond resolution. An empty Neri cache does not imply a cold OS filesystem
or loader cache. Compare medians and retain raw samples; small samples do not
establish reliable tail-latency percentiles. Timing is a measurement, not a flaky
pass/fail threshold.

The cost model is `T = frontend + lookup + codegen + link + startup + program`.
A hit removes codegen and link and reuses an already-created executable. For a
fraction `p` accelerated by a factor `s`, overall speedup is
`1 / ((1 - p) + p / s)`; optimize measured dominant phases first.
This is the application of [Amdahl's law](https://www.cs.cmu.edu/~18742/papers/Amdahl1967.pdf).

## Project object builds

Executable projects partition verified IR by manifest unit. Automatically loaded
standard-library sources form another unit. The compiler gives each class and
its methods one owner; generic specializations and synthetic classes belong to
the executable consumer. Other units carry the external declarations and layouts
needed to call them. Each object uses the same logical module identity, with
strong, executable-local function and class-descriptor symbols.

An object contains its owned function bodies, referenced private string literals,
native imports, and required external declarations. Partition views share immutable
instruction graphs and previously built source maps. The unit containing `main(): Void` supplies
the whole program's runtime import and feature requirements. Other units derive
their feature requirements from their own code and required declarations. This AOT mode has
a conservative minimum runtime ABI of 1.4 for native arrays and classes; imported
capabilities can raise that minimum. Session modules retain their own ABI contract.

On macOS ARM64, object reuse uses the private cache and dependency receipts
described above, with a separate object-cache key domain. The key includes the
unit's canonical transport, manifest contents, owner, target, optimization mode,
runtime manifest, and native tool identities. Native library binaries are resolved
at the final link, which runs on every project build. `--no-cache` compiles every
object. Other supported targets use the same partitioned link without this cache.
Cache lookup hashes the packed canonical payload. A hit skips hexadecimal
transport encoding; a miss reuses that payload and digest to construct the
unchanged transport envelope for the native backend.

The frontend still reads, checks, and lowers the complete project before selecting
objects. This is native-object reuse, not persisted semantic-analysis reuse.
Source tables are tracked per file: a consumer specializing a generic template
depends on that template's source file as well as its generated IR. Splitting
stable definitions from consumer specializations follows the
[Rust compiler's code-generation-unit design](https://rustc-dev-guide.rust-lang.org/backend/monomorph.html).
The correctness of reuse depends on recording all inputs to each compilation
task, as formalized in
[Build Systems à la Carte](https://simon.peytonjones.org/assets/pdfs/build-systems-original.pdf).

For units `U`, misses `M`, and frontend cost `F`, the build cost is
`F + partition + sum(hash(U)) + sum(codegen(M)) + link`. A cache hit removes native
generation for that unit while retaining partition and validation costs. The
granularity tradeoff between reusable results and dependency-tracking overhead
is described in
[Constructing Hybrid Incremental Compilers](https://arxiv.org/pdf/2002.06183),
section 4.1. End-to-end measurements include these costs; cache-hit counts alone
do not establish a latency improvement.

## Session completion

The public completion API analyzes a temporary child of the committed semantic
model. It parses the query and types of referenced bindings; names of other
bindings come from metadata. The shared LSP candidate engine performs symbol
lookup and caps results at 128. Query cost still depends on metadata lookup and
input size; it does not reparse retained application bodies or generate native
objects.

The query source contains typed parameters for referenced bindings instead of access
paths through prior frames. If binding `b` has frame depth `d(b)`, expanding all
access paths contributes `sum(d(b))` path segments; one binding per frame can
therefore contribute `H(H-1)/2` segments after `H` submissions. Typed query
parameters remove that source expansion, while retained symbol lookup remains.

The benchmark measures synthetic semantic queries. RSS includes context preparation, retained analysis and
the runtime. Reproduction commands and workload parameters are in the
[benchmark instructions](../benchmarks/session/README.md#semantic-completion).

## Executable sessions

The Neri-owned session benchmark measures an application initializer and typed
incremental submissions through `ExecutableSession`. The driver is built in
Release mode; submitted modules use `SessionToolchain`'s default Debug mode.
Initializer timing includes preparation, compilation, linking or object loading,
and execution. Submission execution rows exclude separately recorded preparation.

Use a separate new private code-cache directory for each backend and workload.
The first artifact is a Neri code-cache miss; operating-system file, loader and
disk caches retain their own state. The benchmark invokes application initializers
through the session API. Measure application command-line startup and full
console rebuilds separately. Commands, workload parameters and report formats
are in the [session benchmark instructions](../benchmarks/session/README.md).

Dependency-keyed reuse follows the task/rebuild distinction in
[Build Systems à la Carte](https://simon.peytonjones.org/assets/pdfs/build-systems-original.pdf).
The correctness requirement is reuse of a compilation result only for matching
recorded inputs. [A Consistent Semantics of Self-Adjusting Computation](https://arxiv.org/abs/1106.0478)
provides the broader foundation for memoization and change propagation. The run
cache is whole-artifact memoization, not a query-level incremental compiler or
an implementation of that paper's formal proof.

## Compute and scoped tasks

Release kernels retain checked arithmetic, bounds checks and managed collection.
`tasks::generate` executes independent index callbacks on a bounded worker pool,
shares captured graphs through read-only views and joins before returning ordered
results. Task-local allocation and mutation are permitted. Workload definitions,
configuration controls and reproduction commands are in the
[benchmark instructions](../benchmarks/README.md#multicore-execution).

Compute scaling within one run against the implementation's ordinary sequential
loop. Include scheduling, result storage and joining. Record core topology and
virtualization along with worker limits. For allocation workloads, compare GC
modes using the same reference assembly and tiering configuration, and retain
sample distributions and whole-process metrics. Process RSS includes runtime and
JIT memory as well as managed objects. A warmup alone does not prove steady state.

The analytical boundaries are [work/span and Roofline](../benchmarks/README.md#analytical-foundations):
independent work exposes parallelism, while allocation, collection and memory
access can limit scaling. A ratio alone does not identify which cost dominates.

## Managed memory

`neri_gc_pressure` is a deterministic allocation/retention workload built with
the native tests. It keeps one 64 KiB object rooted, allocates and touches 1024
unreachable 64 KiB objects, verifies the retained contents, and explicitly collects
to verify the exact live set and complete reclamation after dropping the root.

The executable reports allocated payload, peak managed bytes, automatic collection
count, peak process RSS, elapsed time, and p50/p95/p99/max allocation latency as
JSON. Allocation latency includes page touches and any synchronous collection;
it is not a measurement of isolated GC pause duration. Managed-byte accounting
includes object headers and payloads, while RSS also includes metadata, native
libraries and allocator effects.

The hard regression budget for this workload is a peak managed heap below 16 MiB,
with at least one automatic collection and complete reclamation of unreachable
objects. The budget permits substantial headroom over the 4 MiB collection floor
while rejecting retention of the complete 64 MiB allocation stream. It is a bound
for this live-set shape, not an application-wide heap limit.

Timing and RSS remain reported measurements rather than pass/fail thresholds.
Repeat runs and stable target-specific histories are required before setting
latency or RSS regression percentages.

## Collections and compiler workloads

Semantic models index class and function names with the compiler's typed
`StringTable`. Lookups index newly appended declarations and resolve local names
before consulting the parent model. The first local declaration wins; session
initializers can append inherited declaration lists before detaching the parent.
The linked lists retain declaration order and the index retains the same symbol
objects. This avoids rescanning every local declaration on each lookup.

IR lowering indexes the bound callable and field names once per lowerer.
Callable lookup preserves function-before-method and local-before-parent
precedence. Field lookup preserves the first declared owner/name pair. Indexes
retain live semantic symbols, and qualified field names are constructed when
indexing rather than on each lookup. Source loading likewise indexes supplied
canonical paths and source identities; each source's library imports are lexed
once during that load.

`scripts/build.sh benchmark` compiles `benchmarks/collections.hk` with the current
compiler and runtime. It compares flat-array append with the compiler's actual
`IntBuffer`, using 2048, 4096 and 8192 elements. Each process performs 16 complete
append-and-traverse rounds and must print the expected sum. One warmup process
precedes five measured processes per case. The report is `build/work.*/collections.jsonl`;
process sidecars retain wall time, user/system CPU time and peak RSS from `wait4`.

The workload enforces exact results and a 30-second process deadline. CPU/RSS
samples are recorded for trend review. Wall time includes the helper's polling
interval; CPU time is the more useful signal for these short processes.

`host::appendByte`, `host::appendInt`, and `host::appendString` return a new flat
array and copy its prior contents. Appending N elements copies N(N-1)/2 existing
elements. A single append is linear, so array append is appropriate for bounded
argument lists but requires a capacity-based or chunked representation for growing
streams. The compiler's linked buffers have constant-time append and cached
sequential traversal; arbitrary indexing still requires traversal.

The compiler fixed-point workload is a correctness gate for automatic collection:
it exercises long-lived graphs and temporary strings while compiling itself with
the current runtime. It does not by itself establish throughput, pause or memory
budgets for general workloads.

## Generic specialization

`examples/generics.hk` exercises a generic box and a generic transformation with
a callback. Its concrete baseline uses the same bodies, with `Box` storing `Int`
and `transform(Int, fn(Int): String): String`; type parameter declarations and
explicit type arguments are removed. Both executables print `The answer is 42`.

Compare alternating builds after warming both variants with
`/usr/bin/time -p scripts/neri.sh build <source> --release --output <path>`;
measure executable sizes with `wc -c`. Timing includes the launcher and native
linker. Record every sample and both source variants. More concrete argument
combinations can increase generated code; this example covers two specializations.

## Agent feedback latency

Project feedback shares bounded content, digest and import observations within
each graph pass. Its final verification uses fresh observations. Diagnostics
run the complete parser and binder without building editor navigation indexes.
Persisted analysis reuse follows source and compiler fingerprints; message
deduplication separately follows the host session, turn and transcript.

Measure a full analysis with a fresh `--state FILE`, then repeat the same
`feedback --project ROOT --codex-hook` invocation and hook input for a reused analysis.
Change `turn_id` while preserving the state file to measure a new delivery scope. Same-scope
repetition returns `{}`; a new scope receives an operation-correlated report.
These are individual wall-time observations, not regression thresholds. A
changed compiler invalidates all unit results; changed sources invalidate their
consumer closures. The first full analysis remains substantially more expensive
than verification of reusable results.
