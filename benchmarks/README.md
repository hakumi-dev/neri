# Performance measurements

Benchmarks measure current contracts, not universal language rankings. Orchestration
and Neri workloads use `.hk`; `reference-dotnet` is only the explicitly comparable
C# workload, not an alternative build or test toolchain for Neri.

## Compute and allocation

Build both programs once from the repository root:

```sh
scripts/neri.sh build benchmarks/compute.hk --release --output build/compute
dotnet build benchmarks/reference-dotnet/Compute.csproj --configuration Release --configfile benchmarks/reference-dotnet/NuGet.Config
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

## Multicore reference

Neri currently executes `batch` sequentially. The C# reference accepts an optional
worker limit and uses a joined `Parallel.For`, one result slot per job, and a
checked final reduction. The one-worker case is the ordinary sequential loop.
Scheduling, worker-result storage and the join are included in the timer.

```sh
./build/compute batch 2000000 64
DOTNET_TieredCompilation=0 dotnet benchmarks/reference-dotnet/bin/Release/net10.0/Compute.dll batch 2000000 64 1
DOTNET_TieredCompilation=0 dotnet benchmarks/reference-dotnet/bin/Release/net10.0/Compute.dll batch 2000000 64 4
DOTNET_TieredCompilation=0 dotnet benchmarks/reference-dotnet/bin/Release/net10.0/Compute.dll batch 2000000 64 14
```

A worker limit is not a promise of that many simultaneously active physical cores.
Compute `S(p) = T(1)/T(p)` and `E(p) = S(p)/p` within one implementation on one
machine. C# scaling is a reference for this workload; it does not demonstrate
parallel Neri execution. Repeat native ARM64 and native x86-64 measurements
separately; emulation is not hardware performance evidence.

## Local observations

The [ARM64 observations](compute-macos-arm64.json) retain raw samples and binary
fingerprints. Initial kernel medians were 62/78 ms (Neri/C# integer), 7/7 ms
(small array, coarse timer resolution), and 43/5 ms (allocation). These are narrow
workloads under the stated .NET configuration, not a language ranking.

In three before/after process pairs, combining Neri's object and metadata
reservations reduced the pooled allocation median from 38 to 28 ms (26% less time,
1.36x throughput). Both variants have linear allocation work; native reservations
per object fall from two to one. No hardware-counter measurement isolates the
contribution of cache locality. This change does not close the allocation gap to
.NET or add concurrent collection.

For the same 64-job batch, the initial C# sweep measured medians of 503, 255, 135,
74, 84 and 72 ms at maximum worker counts 1, 2, 4, 8, 10 and 14. Neri's sequential
median was 391 ms. The 14-worker reference speedup is about 7x relative to its
own sequential case, not 14x. These ordered, single-process observations do not
establish an optimal worker count or explain the 10-worker slowdown; that requires
repeated randomized sweeps and scheduling evidence. Native x86-64 compute/scaling
measurements and parallel Neri measurements are not represented in this baseline.

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

Run-loop, collection-pressure and growing-buffer measurements are documented in
[PERFORMANCE.md](../docs/PERFORMANCE.md). Current language/runtime boundaries are in
[ARCHITECTURE.md](../docs/ARCHITECTURE.md).
