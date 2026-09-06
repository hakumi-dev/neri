# Architecture

Neri has three explicit implementation layers.

## Compiler

`compiler/` is the production compiler and is written entirely in Neri:

- `frontend/` tokenizes and parses source files;
- `semantic/` binds symbols, types, control flow, and diagnostics;
- `ir/` lowers bound programs to canonical Neri IR and provides the command-line driver.

Generic declarations, specialization, and inference live in separate semantic
components. `GenericInference` owns the type-variable bindings for one application.
The binder registers templates before resolving signatures, then processes newly
specialized bodies until the work set is complete. Specialization clones syntax
at type positions and preserves source locations. Concrete classes retain the
existing field layout and precise tracing rules; concrete functions use the same
IR and runtime ABI as ordinary functions. Specializations are cached by qualified
declaration name and canonical type arguments, with bounded expansion.

Function types are structural language types. Closure conversion represents each
signature with a hidden managed class and virtual invocation slot, and each
closure with an implementation class containing its captures. Class
allocation, tracing and indirect dispatch provide closure lifetime and invocation
through the ordinary IR transport and runtime ABI. The signature class is not
source-constructible, and its fallback invocation traps. A callee's `noreturn`
property does not propagate to callers or other implementations of its slot.

`compiler/ir/main.hk` is the executable entry point. `frontend/main.hk` and `semantic/main.hk` are standalone development entry points and are excluded from the compiler source set.

`semantic/readonly.hk` defines transitive readonly views. Binding preserves the
qualifier on reachable references and checks receiver contracts, assignments,
calls, returns and borrowed storage. IR lowering erases view qualifiers without
copying objects or adding runtime wrappers. No LLVM aliasing or purity property
is inferred from a readonly view: mutable aliases may exist elsewhere.
The distinction between read-only access and globally immutable or exclusive
references follows the separation studied by
[Gordon et al. (2012)](https://www.microsoft.com/en-us/research/publication/uniqueness-and-reference-immutability-for-safe-parallelism/).
Neri implements readonly views, not that paper's full isolation type system.

### Effect summaries

`compiler/ir/effects.hk` computes transitive summaries over the lowered call graph,
including virtual-call targets. Each function starts with its local effects and
the safepoint effect of known calls. A callee contributes all its effects except
`noreturn`, which describes that function's control flow, not its callers.

The solver uses reverse call edges and a deduplicated work queue. A summary is a
set of eight effect bits; union only adds information. Each function's set can
grow at most eight times, so the queue terminates even for recursive cycles.
At termination every caller contains its callees' propagated effects. Starting
from local effects and adding only required bits yields the least fixed point.
This is a finite monotone dataflow analysis; the general foundation is
[Kildall's global analysis framework (1973)](https://calhoun.nps.edu/bitstream/10945/42162/1/Kildall_A_unified_approach_1973.pdf).

For `V` functions and `E` call edges, propagation takes `O(V + 8E)` work and
scratch storage takes `O(V + E)`. Building the reverse graph still uses linear
name lookup and can take `O(VE)` work. These bounds describe this pass, not the
whole compiler. Queue and reverse-edge state are private to analysis and are
absent from serialized IR. Read/write summaries do not distinguish local mutable
state from shared state and are not a proof that a closure is safe to run on
another thread.

`compiler/ir/parallel.hk` checks the `parallel fn` effect contract using the same
worklist solver. Its compiler-local graph preserves receiver types before IR
upcasts, including inherited implementations and applicable overrides. Calls
through weaker callback types and unapproved runtime services carry a forbidden
effect marker. Recursive and indirect calls propagate that marker to callback
implementations. This analysis runs only when parallel callback types occur.

Serialized IR retains the conservative union of every virtual-slot implementation
required by the native verifier. The callback analysis graph is discarded after
checking. Capture access remains a semantic type rule; mutable argument ownership
and task lifetime are separate contracts.

## Native boundary

`native/codegen/` is a narrow C++ LLVM consumer. It accepts only verified Neri IR and emits LLVM IR, assembly, or native objects.

`native/runtime/` implements the runtime and precise tracing garbage collector in
C++. Generated programs link to its static archive through the versioned
[runtime ABI](ABI.md).

Language syntax, binding, type rules, diagnostics, and Neri IR lowering are owned by `compiler/`.

## Build driver

`tooling/build.hk` owns native build orchestration, compiler source discovery,
staged bootstrap comparisons, and native/language test selection. The seed compiles
it before it builds the current compiler. CMake builds the native targets.
`native/tools/host.cpp` provides sorted file discovery and subprocess capture with
EOF stdin, separate output channels and a process-group timeout. It contains no
compiler source list, build graph or language expectations.

## Trusted seed

The bootstrap release contains a Neri compiler plus its matching codegen and runtime. It compiles the current sources, and the resulting compiler performs the next verified generation.

## Execution and optimization boundaries

Safe Neri executes with one managed mutator. Native runtime contexts own independent
heaps, root/borrow stacks, collection state and host-error storage. Each native
thread initializes and shuts down its base context explicitly. Runtime reference
stores and tracing enforce context ownership.

The private `native/runtime/task_heap.h` boundary supports scoped child contexts.
The coordinator suspends its heap and registers each task before publishing it to
an executor. The scope waits for all registered tasks, including queued tasks,
before the parent resumes. Each task has a fresh heap and read access to its
suspended ancestors. Child collections trace only child-owned objects; ancestor
heaps remain intact and their mark bits are untouched. Managed stores can retain
ancestor references in child objects, but their destinations must be child-owned.
Native borrows require ownership of the borrowed object.

This task-tree/heap-tree organization and ancestor-directed reference discipline
are described in
[Hierarchical Memory Management for Parallel Programs (2016)](https://www.cs.cmu.edu/~rraghuna/publications/icfp-2016-hierarchical-memory-management-for-parallel-programs/paper.pdf).
Neri uses nonmoving collection and deferred ownership adoption.

Context identity is independent of worker identity: nested execution on a waiting
parent's thread preserves every suspended heap. Tasks may register initialized,
disjoint result-reference spans whose storage remains valid through join. The
runtime roots each span during its task, then collects the child heap with only
that span as roots. Body-owned root frames and native borrows must have ended
before this final collection. Native temporary allocations are reclaimed.
Completed child heaps stay separate until every task in the scope has finished;
the parent then adopts their surviving allocations before resuming. Object
addresses, aliases, cycles and references to ancestors are preserved without
copying payloads. Results from sibling heaps are rejected. Tasks without a result
span reclaim their entire heap at completion.

Adoption updates each survivor's owner and joins its allocation list to the
parent's list: `O(k + n)` work for `k` completed result heaps and `n` surviving
objects, in addition to final child collection. The parent must keep results
rooted across subsequent allocating calls. The native caller guarantees that
output spans are inaccessible through other tasks and captures. Callbacks are
trusted native code; direct pointer writes are outside these checks.

`native/runtime/task_executor.cpp` provides a joined range executor over this
boundary. A nonempty outer call creates at most `min(parallelism, hardware, count)`
participants, including its caller. Nested groups reuse the same pool. Each group
limits its active range invocations, including those waiting for nested groups;
the outer pool bounds the total number of executing threads. Separate native
outer callers own separate pools. Workers are joined when the outer call returns.

A group partitions `[0, count)` into at most four chunks per allowed participant,
with contiguous, disjoint output spans. A mutex-protected LIFO queue supplies
eligible chunks. Waiting coordinators execute eligible work from the same pool
and sleep on a condition variable only when no eligible chunk is queued. This
allows nested groups to progress with one participant or with every participant
already inside a task. Callbacks must not depend on sibling execution order or
block waiting for siblings.

Range calls include worker startup, queue access, task execution and joining.
The [benchmark methodology](../benchmarks/README.md#analytical-foundations)
distinguishes measured execution costs from theoretical work/span bounds.

The native caller is responsible for capture safety and synchronization.
The language's `shared fn` contract restricts access through captured references;
`parallel fn` also checks transitive call effects. Callbacks execute sequentially
in safe Neri. The versioned ABI reserves the
multiple-mutator feature bit and reports it as unsupported.
[Disentanglement in Nested-Parallel Programs (2020)](https://www.cs.cmu.edu/~swestric/20/popl-disentangled.pdf)
distinguishes this memory separation property from race freedom: separation alone
does not justify concurrent mutation of shared data.

Terminal leases and window state are process-wide, thread-confined services.
Native integrations must preserve their operating-system resource ownership and
synchronization requirements across runtime contexts.

LLVM Release uses the O2 module pipeline and generic target CPU settings for
ARM64 and x86-64. Checked arithmetic, bounds and strict floating-point semantics
remain enabled. Native records describe C-compatible memory layouts; they do not
provide a general ownership or cross-thread transfer contract. Native memory access
through `unsafe` does not establish synchronization.

The compiler owns distinct syntax, semantic and IR representations. Binding and
lowering currently operate on a combined program, and those mutable graphs are
not independently scheduled tasks. Generic specialization is cached within a
compilation. The run cache reuses a whole native executable after frontend
verification, rather than incrementally evaluating compiler queries.

Capability boundaries relevant to performance:

| Concern | Current contract |
|---|---|
| Growing collections | Flat append copies the old array; compiler buffers use linked nodes. |
| CPU parallelism | Safe code uses one managed mutator; scoped native execution requires trusted callbacks. |
| Resource lifetime | Native resources require explicit cleanup. |
| Generic abstraction | Concrete specialization and type inference. |
| Data modeling | Classes and native records. |
| Machine targeting | Generic ARM64/x86-64 emission; no language-level SIMD or per-CPU feature selection. |

The [compute benchmarks](../benchmarks/README.md) separate kernel throughput from
run-loop latency and include an independent C# scaling reference. Native x86-64
ABI/codegen CI is a correctness gate, not evidence of Neri multicore scaling.
