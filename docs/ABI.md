# Runtime and IR boundary

The canonical exported declarations and layouts are in
[`runtime_abi.h`](../native/include/neri/runtime_abi.h). Runtime ABI 1.10 uses a
C calling convention on macOS ARM64 and Linux x86-64. Generated programs negotiate
major version, minimum minor version and required feature bits before execution.
The package manifest also identifies the toolchain version and native target.

The `INTERACTIVE_IO` feature (8192) provides generation-scoped terminal
leases, byte input with bounded waiting, terminal dimensions, and monotonic
milliseconds. Platform terminal layouts remain inside the runtime. Key decoding
and the public session API are implemented in Neri.

## Representation

The `EXTENDED_SCALARS` feature (512) defines `Int32`, `UInt32`, and `Float32`, which
occupy four bytes with four-byte alignment; `UInt64` occupies eight bytes with
eight-byte alignment. Their array descriptors identify each scalar kind.
Transport 1.2 carries these types under `extended-scalars-v1`; numeric cast
instructions preserve their checked-conversion contract through native lowering.

Bool and Byte occupy one byte; Int and Float occupy eight bytes; managed references
and pointers occupy eight bytes on both targets. Object headers contain a descriptor
pointer and a runtime-owned word, occupying 16 bytes. Strings and arrays append an
eight-byte length. Strings count UTF-8 bytes; arrays count elements.

Scalar optionals carry a presence tag and aligned payload. Optional managed
references and optional native pointers use a null pointer for absence. Type descriptors specify payload
size/alignment, element layout and tracing behavior. Existing descriptor prefixes
remain accepted according to their declared ABI version and size.

Inline aggregates and multiple mutators have reserved
feature bits. The runtime does not advertise these capabilities. A consumer that
requires an unavailable capability is rejected during negotiation.

## Collection and roots

The collector is precise and nonmoving, with one mutator per heap. Runtime
contexts have independent heap lists, root/borrow stacks, collection state and
host-error storage. Each native thread initializes and shuts down its base
context; shutdown reclaims only the active context's allocations and requires its
roots and borrows to have ended. Process-argument views belong to the context.

Managed allocations carry a private owner identity. Runtime reference stores and
tracing reject objects from unrelated heaps. The runtime's
[private scoped-task boundary](ARCHITECTURE.md#execution-and-optimization-boundaries)
allows child contexts to read suspended ancestor heaps until their scope joins;
registered result spans retain child objects for ownership adoption after all
tasks finish. The low-level scope protocol is private. ABI 1.10 exposes
`neri_rt_v1_task_generate` under `SCOPED_TASKS` (16384): the compiler supplies a
typed callback adapter and an array descriptor. The runtime roots the callback
and fresh output before suspending the caller. Each child roots its disjoint
managed-result span; the adapter stores each callback result without an
intervening safepoint. Join adopts child allocations before the caller resumes.
The native entry trusts the compiler's capture and effect proof; an arbitrary C
callback does not acquire memory safety by calling it.
Static string literals are immortal
and may be referenced by any heap. Ordinary managed strings remain heap-owned,
even though their content is immutable. Raw native pointers do not provide a
managed transfer protocol or make arbitrary foreign memory access safe.

Descriptor trace callbacks
visit managed reference slots. Root frames enter and leave in LIFO order. Live
managed values must be represented in roots or reachable managed fields across
allocating calls. Native local pointer variables are not implicit roots.

Managed stores validate ownership of the destination slot and the stored
reference. Scoped borrows retain the owning allocation and expose a stable address
until the borrow ends. Raw pointers alone do not retain objects. Collection reclaims
unreachable cycles. Shutdown requires all root frames and borrows to have ended.

Each managed object and its private collector metadata share one zero-initialized
native reservation. The prefix preserves the maximum supported payload alignment;
the public object header and payload offsets follow the ABI layouts. Collection frees
the reservation once. Managed-byte statistics count the public header and payload,
while allocator metadata and alignment padding contribute to RSS separately.

Collection runs before an allocation would exceed the heap threshold, on an
explicit call, or on allocation retry after native reservation failure. The
threshold starts at 4 MiB and becomes the larger of 4 MiB and twice the surviving
managed bytes after each collection. This byte count includes object headers and
payloads; metadata and native storage are separate. Managed and native allocation counters are available through
`neri_rt_v1_gc_get_stats`.

Trace callbacks run inside collection and must not allocate managed objects or
reenter collection. Root/borrow bookkeeping and descriptor access allocate no
storage. Slot tracing performs no managed allocation; the collector's mark
worklist can grow through the native allocator.
Runtime contract failures panic; no exception unwinds into Neri code.

## IR transport

The compiler emits canonical Neri IR with transport 1.1, 1.2 for extended
scalars or external library metadata, 1.3 for native records and fixed arrays,
and 1.4 for `scoped-tasks-v1`.
The `native-libraries-v1` feature carries a library
name after each import's source location; empty names retain platform-default
symbol resolution. Only C ABI imports may declare a library. The transport header
includes versions, flags, payload size and a SHA-256 digest. The native reader
validates the envelope and the typed program before constructing LLVM objects.
Malformed, unsupported and incompatible inputs produce stable NIR diagnostics.

`task.generate<R>` (61) returns `R[]` and carries a virtual invoke-slot symbol,
an unsafe capability, count, parallelism, and callback. The capability makes the
source compiler's noninterference proof an explicit trust boundary. Native
verification checks the callback signature, operand/result types, required
feature and conservative virtual-call effects. The source compiler separately
checks shared captures and parallel-call effects before emitting the operation.

`native-records-v1` appends native declarations after the function vector, sorted
by name. Each declaration carries its identity, struct/union flag and ordered
fields. Native record references use type tag 20 and symbol kind 10; fixed arrays
use type tag 21 followed by a 32-bit element count and element type. The backend
recomputes size, alignment and field offsets, rejects recursive inline layouts,
and keeps native record identities distinct from managed classes.

`native.field.address` (59) returns the typed address of a declared field.
`native.index.address.checked` (60) bounds-checks a fixed-array index before
returning the element address. Both require an unsafe capability. Native records
use inline value storage; C imports exchange them through pointers.

`neri-codegen` owns LLVM lowering and target emission. Native runtime symbols
and C imports remain explicit IR imports. Debug and Release preserve checks and
strict floating-point semantics. Reproducibility compares canonical NIR, native
objects and linked compiler executables independently.
