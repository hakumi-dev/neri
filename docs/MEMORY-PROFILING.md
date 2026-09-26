# Runtime memory profiling

Set `NERI_MEMORY_PROFILE=1` before starting a Neri program to enable runtime
memory diagnostics. The runtime reads this setting on its first profiling check
and writes one JSON object to standard error at each `neri_rt_v1_shutdown` call.
The `event` is `neri_memory_profile`; `scope` is `process_cumulative`.
An embedding process that initializes and shuts down the runtime more than once
receives a cumulative snapshot at each shutdown. Child processes produce their
own profiles.

For example, run `NERI_MEMORY_PROFILE=1 ./build/application` and retain standard
error. A long-running server emits its snapshot when it shuts down normally;
this setting does not stream live samples. Programs must be linked against a
runtime that supports profiling. Changing the environment does not replace the
runtime already linked into an executable.

`managed_allocated_*` counts successful managed allocations across all heaps,
including task heaps. Managed bytes include the object header and payload.
They exclude allocator metadata, native allocations and process RSS.
`managed_peak_outstanding_*` is the largest process-wide total still held by the
managed allocator. `managed_outstanding_*` is the total immediately before the
current shutdown releases its heap. Outstanding objects can include garbage that
has not yet been collected, so these fields do not measure retained reachable
objects. `last_collected_heap_survivor_*` describes only the most recently
collected heap, which may be a worker heap.

`managed_reclaimed_*` counts objects freed by garbage collection. Releasing a
heap at shutdown or at the end of a task lowers outstanding totals but does not
count as collection reclamation. The `types` object groups allocations by a
copied type descriptor name and reports allocated, reclaimed and outstanding
counts and bytes per type. Task result adoption changes heap ownership without
counting an allocation or reclamation twice.

`gc_pause_total_ns` sums elapsed collection durations across heaps and threads;
concurrent collections can overlap. `gc_pause_max_ns` is the longest individual
collection. These fields include profiling work performed during collection and
are not a process-wide stop-the-world interval. `string_concat_calls`,
`string_concat_empty_operand_calls` and `string_concat_result_bytes` count
successful string concatenations and their result payload bytes.

Profiling adds locking and per-type accounting on the enabled path, so compare
instrumented runs with other instrumented runs. With the setting disabled, no
profile is emitted. The profiler retains its aggregate state until process exit.

Measure elapsed time and RSS again with profiling disabled. Keep the workload,
Release settings, host and database inputs equal across variants. Run database
benchmarks on disposable databases, with compilation outside the timed interval.
Allocation volume, surviving objects and process memory answer different
questions; see Microsoft's [GC performance guidance](https://learn.microsoft.com/en-us/dotnet/standard/garbage-collection/performance).

## Compiler memory boundaries

The compiler's integer buffers store scalar values in geometrically growing
arrays. Their public operations address positions rather than linked-node
identities. Syntax and bound arenas keep their node identity contracts. Empty
syntax metadata arrays can share zero-length storage; replacing one field with
a populated array does not change another field.

IR byte buffers accumulate chunks while writing. Text and hexadecimal output
materialize one exact-sized byte array and validate UTF-8 once, including
characters crossing a chunk boundary. Hexadecimal output is ASCII and asserts
that decoding succeeds. The transport bytes and cache digest remain unchanged.
This avoids repeatedly copying intermediate strings during final conversion.

Measure native cache misses and hits separately: a cache lookup serializes the
binary payload for its digest, while hexadecimal conversion runs only when code
generation needs the transport. A warm run cannot establish a cold-path speedup.
Retained session models and loaded modules still support later declarations,
generic specialization and rollback; these allocation changes do not shorten
their ownership lifetime or unload code earlier.

The design follows the allocation and identity tradeoffs in the
[LLVM container guidance](https://llvm.org/docs/ProgrammersManual.html#picking-the-right-data-structure-for-a-task).
[Boehm, Atkinson and Plass (1995)](https://research.google/pubs/ropes-an-alternative-to-strings/)
provides background on avoiding repeated string copies. Neri continues to use
flat language strings; the compiler changes their construction, not their type.
