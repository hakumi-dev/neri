# Performance measurements

Benchmarks measure current contracts, not universal language rankings. Orchestration
and Neri workloads use `.hk`; `reference-dotnet` is only the explicitly comparable
C# workload, not an alternative build or test toolchain for Neri.

## Compute and allocation

Build both programs once from the repository root:

```sh
scripts/neri.sh build benchmarks/compute.hk --release --output build/compute
(cd benchmarks/reference-dotnet && dotnet build --configuration Release --configfile NuGet.Config)
```

Run matching cases, sequentially on an otherwise idle host:

```sh
./build/compute integer 20000000 1
DOTNET_TieredCompilation=0 dotnet benchmarks/reference-dotnet/bin/Release/net10.0/Compute.dll integer 20000000 1
./build/compute array 4096 4096
DOTNET_TieredCompilation=0 dotnet benchmarks/reference-dotnet/bin/Release/net10.0/Compute.dll array 4096 4096
./build/compute allocation 100000 8
DOTNET_TieredCompilation=0 dotnet benchmarks/reference-dotnet/bin/Release/net10.0/Compute.dll allocation 100000 8
```

Both implementations use signed 64-bit checked arithmetic and checked array access.
The .NET project enables overflow checking explicitly. Disabling tiered compilation
selects an optimized-JIT control configuration without later tier promotion or
dynamic PGO; it is not the default .NET configuration. Repeat with that environment
variable absent to characterize default tiering separately. See Microsoft's
[compilation settings](https://learn.microsoft.com/en-us/dotnet/core/runtime-config/compilation).

Each process executes one unreported warmup and seven measured kernel invocations.
Parsing, array construction, oracle evaluation, reporting, compilation and process
startup are outside the kernel timer. GC triggered within the allocation kernel
is included; there is no forced collection between samples. Millisecond samples
below 10 ms have coarse resolution. Within-process samples share allocator and
JIT history; repeat independent processes in alternating language/variant order
before inferring improvements. Report hardware, runtime/compiler versions and raw
samples, rather than asserting timing thresholds in tests.

The contracts are:

- `integer`: `x[0] = 1`, `x[n+1] = (17*x[n] + 23) mod 1000003`.
  An independent exponentiation-by-squaring oracle checks
  `x[n] = 17^n + 23*(17^n - 1)/16` in modular arithmetic. This loop has a serial
  dependency chain and is not itself a parallel or memory-bandwidth workload.
- `array`: initialize `a[i] = i`, then increment every element and sum the new
  values for `r` rounds. The checksum is `r*n*(n-1)/2 + n*r*(r+1)/2`.
  The default array occupies 32 KiB: this is a small-array kernel, not a DRAM
  bandwidth or full Roofline measurement. Neri's flat-append setup is excluded.
- `allocation`: build a linked chain of `n` objects and traverse it for each of
  `r` rounds. The checksum is `r*n*(n-1)/2`. The chain is live while being built;
  a later round makes the previous chain unreachable. Allocation, reference
  stores, traversal and any triggered collection are all timed.
- `batch`: sum the independent integer kernels with counts `n+j` for jobs
  `j = 0..r-1`. Changing each count prevents loop-invariant reuse of identical
  pure calls. Each job is independently checked by the logarithmic oracle.

## Multicore execution

Both programs accept an optional worker limit for `batch`. Neri uses
`tasks.generate`; the C# reference uses a joined `Parallel.For`. Both use one
result slot per job and a checked final reduction. The one-worker case is the
ordinary sequential loop, so scaling includes the cost of introducing task
execution. Scheduling, worker-result storage and the join are inside the timer.
Neri creates and joins its worker threads per call; .NET may reuse pool threads.

```sh
./build/compute batch 2000000 64
./build/compute batch 2000000 64 4
./build/compute batch 2000000 64 14
DOTNET_TieredCompilation=0 dotnet benchmarks/reference-dotnet/bin/Release/net10.0/Compute.dll batch 2000000 64 1
DOTNET_TieredCompilation=0 dotnet benchmarks/reference-dotnet/bin/Release/net10.0/Compute.dll batch 2000000 64 4
DOTNET_TieredCompilation=0 dotnet benchmarks/reference-dotnet/bin/Release/net10.0/Compute.dll batch 2000000 64 14
```

A worker limit is not a promise of that many simultaneously active physical cores.
Compute `S(p) = T(1)/T(p)` and `E(p) = S(p)/p` within one implementation on one
machine. Each language's measurements establish only its own scaling for this
workload. Repeat native ARM64 and native x86-64 measurements
separately; emulation is not hardware performance evidence.

## Repeated native comparison

`compute-matrix.hk` runs the four kernels in four configurations: Neri Release,
default .NET JIT, optimized JIT with tiering disabled, and .NET NativeAOT. Each
configuration occupies every position once across four independent process
repetitions. Worker counts are powers of two up to a supplied limit, including
the limit itself; their order reverses in alternate repetitions. This is a
deterministic balanced order, not a random
sample of machine conditions.

```sh
scripts/neri.sh build benchmarks/compute.hk --release --output build/compute
scripts/neri.sh build benchmarks/compute-matrix.hk --release --output build/compute-matrix
(cd benchmarks/reference-dotnet && dotnet build --configuration Release --configfile NuGet.Config)
(cd benchmarks/reference-dotnet && dotnet publish --configuration Release --runtime osx-arm64 -p:PublishAot=true --configfile NuGet.Config --output ../../build/compute-aot)
mkdir -p build/compute-results
./build/compute-matrix build/native/native-release/neri-host ./build/compute benchmarks/reference-dotnet/bin/Release/net10.0/Compute.dll ./build/compute-aot/Compute build/compute-results 14
```

Build the native helper with `scripts/build.sh native-test` if it is unavailable.
Use an empty output directory for each experiment. The matrix uses 20 million
integer steps, a 4096-element array with 10000 rounds, 100000 allocations with
16 rounds, and 64 batch jobs starting at 2 million steps. Round counts control
timer quantization; compare only matching parameters. Each kernel invocation
checks its result.

Every child has a 120-second deadline. Nonzero exits, signals, timeouts and helper
failures stop the experiment. Named files retain the seven kernel samples, stderr,
exit status, and whole-process CPU time, wall time and peak RSS. `order.tsv`
records process order and configuration. Process metrics include warmup, setup
and reporting; they are not interchangeable with kernel timings. The matrix
clears tiering/PGO environment overrides before selecting its JIT configuration;
other runtime settings and host load remain experiment conditions to record.
Default JIT samples can include tier promotion and are not asserted to be steady
state. Build .NET from `reference-dotnet/`: its `global.json` selects SDK 10.0.302
exactly, while the project selects runtime 10.0.10 with runtime roll-forward
disabled. Installing an SDK alone does not select it when newer SDKs are present.
Each C# process records its actual runtime version, process architecture, dynamic
code support and server-GC setting on stderr, outside the kernel timer. The host
version in `dotnet --info` is distinct from the application's selected runtime;
see Microsoft's [version selection rules](https://learn.microsoft.com/en-us/dotnet/core/versions/selection).
NativeAOT compiles at publish time; see Microsoft's
[NativeAOT contract](https://learn.microsoft.com/en-us/dotnet/core/deploying/native-aot/).

The manual `Native compute observations` workflow verifies the frontend on
macOS, exports canonical IR, and builds and runs x86-64 machine code on an
Ubuntu x86-64 runner. It verifies source/IR hashes and the commit before lowering,
runs native contracts, and retains host identity, tool versions, binary hashes
and raw results as artifacts. The Linux job compares .NET SDK 10.0.302 JIT and
NativeAOT on the same host with worker limits 1, 2 and 4. No emulation is involved;
the [GitHub-hosted environment](https://docs.github.com/en/actions/reference/runners/github-hosted-runners)
is a virtual machine, not a dedicated bare-metal PC. This measures kernel
execution, not a Linux self-hosted frontend, and defines no CI timing threshold.

### Measurement records

Records retain workload parameters, source and binary fingerprints, hardware,
runtime identity, raw samples and process metrics. They are evidence for the
specified configurations, not a ranking of languages.

| Record | Scope |
|---|---|
| [ARM64 kernels and allocator comparison](compute-macos-arm64.json) | Kernel samples and paired allocator variants. |
| [ARM64 matrix](compute-matrix-macos-arm64.json) | 112 processes, 784 checked kernel samples, M4 Pro. |
| [Runtime-pinned ARM64 matrix](compute-matrix-macos-arm64-pinned.json) | 112 processes with actual .NET runtime identity and substantial host variability. |
| [Native x86-64 matrix](compute-matrix-linux-x86_64.json) | 88 processes, 616 checked samples, four-vCPU AMD EPYC VM. |

Summarize each process by its median, then compare those process summaries.
Keep process and kernel timers separate. Absolute times require matching workload
and host conditions; runtime pinning alone does not control power mode or background
load. Record power and load before and after workstation runs. The runtime-pinned
ARM64 data contain large changes within the same binary: no per-sample telemetry
isolates their cause. Neither VM results nor local Mac samples establish
dedicated-PC performance.

## Compiler effect analysis

`compiler-effects.hk` generates ascending call chains of 128, 256, 512 and 1024
functions and compares two compiler executables on the same source. Each size has
one warmup pair and four measured process pairs with balanced variant order.
The native helper records whole-process wall time, user/system CPU time and peak
RSS. Every invocation has a 120-second deadline, must succeed, and must produce
byte-identical canonical IR across variants. Source generation and comparison are
outside the child-process measurements; parsing, binding, lowering and IR emission
are included. These are compiler timings, not program execution timings.

```sh
scripts/neri.sh build benchmarks/compiler-effects.hk --release --output build/compiler-effects
mkdir -p build/effects-results
./build/compiler-effects build/native/native-release/neri-host /path/to/compiler-a /path/to/compiler-b build/effects-results
```

Use immutable compiler binaries, an empty output directory, matching standard
libraries and an otherwise idle host. Retain binary hashes and process records.
The forward chain stresses transitive propagation; it does not represent every
program shape or isolate the time spent in a single compiler pass.

The [ARM64 effect-analysis record](compiler-effects-macos-arm64.json) contains
all 40 process measurements, compiler/source hashes and host conditions. Its
paired variants must satisfy the same canonical-IR contract. Timing values are
descriptive evidence; no timing threshold is part of the test suite.

## Runtime heap isolation

The [ARM64 heap comparison](runtime-heaps-macos-arm64.json) records four balanced
process pairs for each of the integer, array and allocation kernels. Each process
performs one warmup and seven checked samples. Binary/source hashes and parameters
identify the global-heap and thread-local-heap variants; both use Release mode.
The median process medians are 92/92 ms for integer, 22.5/22.5 ms for array and
77/82.5 ms for allocation (global/thread-local). The allocation workload includes
reference stores and collection; no measurement isolates the cost of TLS lookup
from ownership checking. These local battery-powered observations quantify a
tradeoff, not a throughput improvement or parallel scaling result.

`scripts/build.sh native-test --thread-sanitize` instruments runtime heap access
and the native worker probe with ThreadSanitizer. The probe checks simultaneous
independent collection and rejects foreign references through stores and roots.
It also checks ancestor reads during child collection, nested execution on a
waiting thread, join-before-resume, and rejection of ancestor writes and borrows.
Returned graphs preserve identity and cycles across nested joins, become
parent-owned, and are reclaimed when their roots disappear. Sibling result
references and unfinished body roots/borrows are rejected before adoption.
The range executor probe covers uneven, exactly-once partitioning, an empty group,
nested result adoption, one-participant progress and a saturated two-participant
join. It records participating threads to check the outer pool bound. These are
correctness contracts, not parallel Neri throughput measurements.
It uses C because native entry callbacks are not expressible in Neri's current
C ABI. Address/undefined-behavior checks use the separate `--sanitize` build.

The [ARM64 task-context comparison](task-heaps-macos-arm64.json) contains three
paired runtime variants, 72 processes and 504 checked kernel samples. Each pair
links the same program object against the independent-thread baseline and a
scoped-context runtime. The current variant uses a constant-initialized active
context pointer, initializes its base context on first access, and retains a heap
reference throughout allocation and collection. Its median process medians are
60/60 ms for integer, 16/16 ms for array and 56/59 ms for allocation
(baseline/current). The roughly 5% allocation cost includes context and ownership
checks; these measurements do not isolate their individual costs.

The record also includes dynamically initialized TLS and lazy-base variants
(allocation medians 57/100 and 57/61 ms within their respective pairs).
Dynamic TLS guards and repeated heap lookup can materially affect an allocation
path even when the language-level program is unchanged. Results are descriptive
observations on AC power, not timing gates, confidence intervals or evidence of
parallel Neri scaling.

## Analytical foundations

- [Amdahl (1967)](https://www.cs.cmu.edu/~18742/papers/Amdahl1967.pdf): for an
  accelerated fraction `f` and local speedup `s`, total speedup is
  `1 / ((1-f) + f/s)`. Improving run-cache hits is distinct from speeding up a
  long-running program.
- [Blumofe and Leiserson, work stealing](https://www.cs.cornell.edu/courses/cs612/2006sp/papers/blumofe94.pdf):
  work `W` and span `D` give the lower bound `max(W/p, D)` and, under the paper's
  fully strict computation/scheduler assumptions, expected time `W/p + O(D)`.
  This is not a guarantee for arbitrary I/O, shared mutable captures or
  heterogeneous cores. The batch reference has independent jobs and a final join.
  The native range executor uses a shared queue and native-stack helping;
  the randomized work-stealing bound applies to the paper's scheduler, not to
  this executor. [Cilk (1995)](https://publications.csail.mit.edu/lcs/pubs/pdf/MIT-LCS-TM-548.pdf)
  describes explicit continuations and per-processor ready pools.
- [Williams, Waterman and Patterson, Roofline](https://www2.eecs.berkeley.edu/Pubs/TechRpts/2008/EECS-2008-134.pdf):
  attainable compute throughput is bounded by `min(peak compute, bandwidth *
  arithmetic intensity)`. More workers cannot remove a saturated memory path.
  No peak/bandwidth roof is inferred from the small-array case.
- [Leijen, Zorn and de Moura, mimalloc (2019)](https://www.microsoft.com/en-us/research/wp-content/uploads/2019/06/mimalloc-tr-v1.pdf):
  allocator fast paths, locality and thread-local allocation are relevant to
  runtime design. Neri's contiguous object/metadata reservation is a small local
  optimization, not an implementation of mimalloc or a concurrent allocator.
- [Apple silicon tuning guidance](https://developer.apple.com/documentation/apple-silicon/tuning-your-code-s-performance-for-apple-silicon/):
  Apple performance and efficiency cores require host-specific measurement and
  appropriate scheduling. Uniform-worker mathematical bounds alone do not model
  the relative speeds of heterogeneous cores.
- [Kalibera and Jones, Rigorous Benchmarking in Reasonable Time (2013)](https://kar.kent.ac.uk/33611/45/p63-kaliber.pdf):
  variation between process executions and variation within one process are
  distinct levels. The matrix retains both levels; summarize process medians
  before comparing configurations rather than treating all seven samples as
  independent experiments. Four processes and one binary build do not establish
  confidence intervals across builds, hosts or scheduling conditions.
  Their [hierarchical random-effects treatment](https://arxiv.org/abs/2007.10899)
  explains why a ratio of observed times alone does not quantify uncertainty;
  the descriptive medians here are not that paper's inferential procedure.
- [Clebsch et al., Orca: GC and Type System Co-Design for Actor Languages (2017)](https://www.ponylang.io/media/papers/orca_gc_and_type_system_co-design_for_actor_languages.pdf):
  isolation and reference capabilities are part of the collector's correctness
  assumptions, not just scheduler optimizations. This motivates evaluating Neri's
  capture/sharing rules and heap ownership together before enabling parallel
  managed execution. Neri currently implements neither Orca nor an actor type system.

Run-loop, collection-pressure and growing-buffer measurements are documented in
[PERFORMANCE.md](../docs/PERFORMANCE.md). Current language/runtime boundaries are in
[ARCHITECTURE.md](../docs/ARCHITECTURE.md).
