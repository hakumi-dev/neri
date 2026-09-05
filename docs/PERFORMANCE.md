# Memory and performance checks

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
