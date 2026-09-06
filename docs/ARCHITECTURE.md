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

Safe Neri executes with one managed mutator. At the native boundary, each thread
has an independent heap, root/borrow stacks, collection state and host-error
storage. Runtime reference stores and tracing enforce heap ownership. Each native
thread initializes and shuts down explicitly; there is no cross-heap transfer
protocol. A closure retaining an object does not make it safe to share with
another thread. The ABI reserves but does not advertise multiple-mutator support;
safe Neri has no task, atomic, transfer or synchronization API.

Terminal leases and window state are process-wide, thread-confined services.
Independent heaps do not permit concurrent terminal/window access or remove
operating-system resource ownership requirements. Native callbacks remain an
unsafe integration boundary. ThreadSanitizer checks the independent-heap contract;
it does not prove safety for every native service or arbitrary user callback.

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

Current capability boundaries relevant to performance:

| Concern | Current contract |
|---|---|
| Growing collections | Flat append copies the old array; compiler buffers use linked nodes. General capacity-based typed collections are tracked in [#28](https://github.com/hakumi-dev/neri/issues/28). |
| CPU parallelism | One managed mutator; bounded concurrency and sharing rules are tracked in [#32](https://github.com/hakumi-dev/neri/issues/32). |
| Resource lifetime | Native resources require explicit cleanup; automatic scopes are tracked in [#29](https://github.com/hakumi-dev/neri/issues/29). |
| Generic abstraction | Concrete specialization and inference; explicit interfaces and constraints are tracked in [#24](https://github.com/hakumi-dev/neri/issues/24). |
| Data modeling | Classes and native records; immutable domain values and closed sum types are tracked in [#26](https://github.com/hakumi-dev/neri/issues/26) and [#25](https://github.com/hakumi-dev/neri/issues/25). |
| Machine targeting | Generic ARM64/x86-64 emission; no language-level SIMD or per-CPU feature selection. |

The [compute benchmarks](../benchmarks/README.md) separate kernel throughput from
run-loop latency and include an independent C# scaling reference. Native x86-64
ABI/codegen CI is a correctness gate, not evidence of Neri multicore scaling.
