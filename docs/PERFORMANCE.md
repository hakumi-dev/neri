# Memory and performance checks

## Run latency

`neri source.hk --timings` separates frontend, cache lookup, code generation,
linking and program execution. `--no-cache` provides an uncached comparison with
the same compiler, runtime and safety checks. A cache hit still parses, type-checks,
lowers and verifies the program; it skips native code generation and linking.

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

The isolated [macOS ARM64 measurement](../benchmarks/arena-index-macos-arm64.json)
reduced the Sumi console frontend from 15.049 s to 3.632 s and the complete build
from 26.07 s to 14.69 s, with the same native backend. Native generation remained
about 10.7 s. This is a sequential local comparison, not a latency percentile or
a general memory-reduction claim.

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

The [macOS ARM64 baseline](../benchmarks/run-latency-macos-arm64.json) records a
small class-based Hello program on an Apple M4 Pro, macOS 26.6.2 and LLVM 22.1.8:
the ten-sample median was 407 ms uncached and 46 ms cached (8.8x), including
`scripts/neri.sh`. The first empty-cache run was 474 ms. This is a local run-loop
observation, not a general application-throughput or first-build speedup claim.

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

The [macOS ARM64 project measurement](../benchmarks/aot-project-macos-arm64.json)
recorded 15.67 s with an empty object cache and 5.21 s with all five objects
reused. Maximum resident memory was 830 MB and 472 MB respectively. Frontend
work remained about 3.7 s in both builds. These are single sequential build
observations, not complete `sumi c` startup measurements or latency percentiles.

With the validated toolchain installed, Sumi's separate `prepare-console` command
took 17.90 s. Two subsequent `sumi c` invocations reached `app ready` and exited
on EOF in 2.35 s and 0.84 s. These include application initialization and shutdown.
Object keys include owner and configuration paths; Sumi's fresh preparation
directory can therefore cause conservative misses between preparations. The
stable-project cache measurement above isolates Neri's object reuse.

## Executable sessions

The Neri-owned session benchmark measures an application initializer and typed
incremental submissions through `ExecutableSession`. On an Apple M4 Pro running
macOS 26.6.2, a three-repetition Sumi application fixture produced these final
local observations with the object backend. The benchmark executable is built
in Release mode; submitted modules use `SessionToolchain`'s default Debug mode:

| Operation | First cache miss | Two cache hits |
|---|---:|---:|
| Initializer, complete operation | 1,260 ms | 600 ms, 596 ms |
| New-variable execute | 36 ms | 15 ms, 17 ms |

The initializer measurement includes preparation, compilation, linking or object
loading, and execution. Submission execution rows exclude their separately
recorded preparation, which was 32–36 ms for these operations. Uncached execution
for the other four submissions was 38–42 ms. At retained-history positions 1,
10 and 25, uncached execution was 39, 40 and 51 ms; preparation was 37, 41 and
56 ms. Cache-hit execution at those positions was 15–17, 18–22 and 25 ms.

A generated project matrix, measured before packed transport storage, separated
application size from the new-variable submission. For 10, 100, 500 and 900 ordinary functions, initializer misses were
60, 165, 781 and 1,467 ms, while new-variable misses were 23, 25, 39 and 52 ms.
Their actual cache hits were 4, 5, 7 and 10 ms. This local matrix shows that the
remaining initializer work grows with the application while incremental work
grows much more slowly over the measured range.

Each backend used a separate new private code-cache directory. The first artifact
is therefore a Neri code-cache miss, but neither run clears operating-system file,
loader or disk caches. This benchmark invokes the Sumi initializer through the
Neri session API; it does not measure startup of an installed Sumi command-line
program. Raw records (`build/wire-final-session.csv` and `.jsonl`) and exact reproduction
metadata are described in
[`benchmarks/session/README.md`](../benchmarks/session/README.md).

An installed Sumi command may also build its console executable after a toolchain
update. A separate Release build of the same Sumi console sources with the same
native backend measured 40.83 s and 7,881,021,320 bytes of peak memory footprint
with the previous transport encoder, versus 30.12 s and 789,480,432 bytes with
packed storage and direct hexadecimal encoding. The 38,349,058-byte hexadecimal
transport matched byte for byte. These are single sequential build observations,
not latency percentiles. Footprint is process memory reported by macOS
`/usr/bin/time -lp`, not disk-cache size. Sumi controls when this build runs;
these changes reduce Neri's compilation cost.

An earlier `e720a92` record measured complete initializer operations at 2,164,
2,224 and 2,335 ms and new-variable preparation plus execution at 1,951, 1,875
and 2,200 ms. That record predates cache-hit telemetry and uses a different
compiler, runtime and session implementation, so it is historical context rather
than a matched before-and-after attribution.

Dependency-keyed reuse follows the task/rebuild distinction in
[Build Systems à la Carte](https://simon.peytonjones.org/assets/pdfs/build-systems-original.pdf).
The correctness requirement is reuse of a compilation result only for matching
recorded inputs. [A Consistent Semantics of Self-Adjusting Computation](https://arxiv.org/abs/1106.0478)
provides the broader foundation for memoization and change propagation. The run
cache is whole-artifact memoization, not a query-level incremental compiler or
an implementation of that paper's formal proof.

## Compute and scoped tasks

Release kernels retain checked arithmetic, bounds checks and managed collection.
`tasks.generate` executes independent index callbacks on a bounded worker pool,
shares captured graphs through read-only views and joins before returning ordered
results. Task-local allocation and mutation are permitted. Workload definitions,
raw samples, compiler/runtime fingerprints and host conditions are in the
[measurement records](../benchmarks/README.md#measurement-records).

Representative Neri process-median summaries are:

| Workload and host | One worker | Worker limit | Parallel time | Speedup |
|---|---:|---:|---:|---:|
| Integer batch, M4 Pro | 390 ms | 14 | 44 ms | 8.86x |
| Integer batch, EPYC 9V74 VM | 690 ms | 4 | 181 ms | 3.81x |
| Allocation batch, M4 Pro, GC-mode matrix | 162.5 ms | 14 | 29 ms | 5.60x |
| Allocation batch, EPYC 7763 VM, GC-mode matrix | 262 ms | 4 | 144.5 ms | 1.81x |

These are within-run ratios against each implementation's ordinary sequential
loop, including parallel scheduling, result storage and joining. The M4 Pro has
ten performance and four efficiency cores; the Linux hosts are four-vCPU virtual
machines. These results do not establish a universal processor optimum or
dedicated-PC performance. The two EPYC records use different hosts and cannot
isolate a compiler change by comparing absolute times.

Allocation is a material limit. In the matched GC-mode matrix, default-JIT C#
with server GC is faster than Neri for the allocation workload on both hosts.
On the Mac, its best measured worker limit is eight, with a 7 ms median and
19.55 ms mean sample time, versus Neri's 29 ms median and 32.33 ms mean at fourteen.
Their median whole-process peak RSS values are approximately 644 and 110 MiB.
On the EPYC 7763 VM at four workers, server JIT and Neri have 66 and 144.5 ms
medians, with approximately 116 and 52 MiB median peak RSS. Process RSS includes
runtime and JIT memory; it is not the size of live objects or isolated GC overhead.

GC mode changes the throughput/memory tradeoff. Workstation and server GC use
the same reference assembly and default tiering. Server GC is not faster at every
worker count: on the EPYC VM, its one-worker median is 180 ms versus workstation
GC's 72.5 ms. Large variation in server-GC samples makes medians insufficient as
a description of throughput or pauses; records also retain means, maxima and
whole-process metrics. One warmup and six processes do not prove steady state.

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

A local macOS ARM64 Release observation with LLVM 22.1.8 on 2026-09-05 measured:

| Metric | Measurement |
|---|---:|
| Peak managed bytes | 4,129,776 |
| Automatic collections | 16 |
| Peak RSS bytes | 5,652,480 |

These are individual process observations, not a cross-machine performance claim.
Timing and RSS remain reported measurements rather than pass/fail thresholds.
Repeat runs and stable target-specific histories are required before setting
latency or RSS regression percentages.

## Collections and compiler workloads

`scripts/build.sh benchmark` compiles `benchmarks/collections.hk` with the current
compiler and runtime. It compares flat-array append with the compiler's actual
`IntBuffer`, using 2048, 4096 and 8192 elements. Each process performs 16 complete
append-and-traverse rounds and must print the expected sum. One warmup process
precedes five measured processes per case. The report is `build/work.*/collections.jsonl`;
process sidecars retain wall time, user/system CPU time and peak RSS from `wait4`.

The raw samples and source fingerprints are in
`benchmarks/baseline-macos-arm64.json`. An Apple M4 Pro Release baseline with
LLVM 22.1.8 gives these five-sample medians:

| Elements | Array CPU seconds | Buffer CPU seconds | Array RSS bytes | Buffer RSS bytes |
|---|---:|---:|---:|---:|
| 2048 | 0.016418 | 0.005178 | 20,938,752 | 4,669,440 |
| 4096 | 0.050169 | 0.007733 | 41,058,304 | 7,815,168 |
| 8192 | 0.170351 | 0.012964 | 49,315,840 | 11,632,640 |

The workload enforces exact results and a 30-second process deadline. CPU/RSS
samples are recorded for trend review; the table is a target-specific baseline,
not a universal throughput guarantee. Wall time includes the helper's polling
interval; CPU time is the more useful signal for these short processes.

`host.appendByte`, `host.appendInt`, and `host.appendString` return a new flat
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

A local macOS ARM64 Release comparison on 2026-09-05 with LLVM 22.1.8 measured
five alternating builds after initial builds of both variants:

| Variant | Build wall seconds (five samples) | Median | Executable bytes |
|---|---|---:|---:|
| Generic | 0.49, 0.13, 0.16, 0.15, 0.14 | 0.15 s | 98,800 |
| Concrete | 0.14, 0.13, 0.15, 0.15, 0.13 | 0.14 s | 98,704 |

Builds used `/usr/bin/time -p scripts/neri.sh build <source> --release --output <path>`;
sizes used `wc -c`. Timing includes the launcher and native linker. The 96-byte
size difference and noisy short build times describe this two-specialization
example only; they are not regression thresholds or a projection for large
generic programs. More concrete argument combinations can increase generated code.
