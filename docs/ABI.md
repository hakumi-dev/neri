# Runtime and IR boundary

The canonical exported declarations and layouts are in
[`runtime_abi.h`](../native/include/neri/runtime_abi.h). Runtime ABI 1.8 uses a
C calling convention on macOS ARM64 and Linux x86-64. Generated programs negotiate
major version, minimum minor version and required feature bits before execution.
The package manifest also identifies the toolchain version and native target.

ABI 1.8 adds the `INTERACTIVE_IO` feature (8192): generation-scoped terminal
leases, byte input with bounded waiting, terminal dimensions, and monotonic
milliseconds. Platform terminal layouts remain inside the runtime. Key decoding
and the public session API are implemented in Neri.

## Representation

Bool and Byte occupy one byte; Int and Float occupy eight bytes; managed references
and pointers occupy eight bytes on both targets. Object headers contain a descriptor
pointer and a runtime-owned word, occupying 16 bytes. Strings and arrays append an
eight-byte length. Strings count UTF-8 bytes; arrays count elements.

Scalar optionals carry a presence tag and aligned payload. Optional managed
references use a null reference for absence. Type descriptors specify payload
size/alignment, element layout and tracing behavior. Existing descriptor prefixes
remain accepted according to their declared ABI version and size.

Inline aggregates, additional scalar widths and multiple mutators have reserved
feature bits. The runtime does not advertise these capabilities. A consumer that
requires an unavailable capability is rejected during negotiation.

## Collection and roots

The collector is precise, nonmoving and single-mutator. Descriptor trace callbacks
visit managed reference slots. Root frames enter and leave in LIFO order. Live
managed values must be represented in roots or reachable managed fields across
allocating calls. Native local pointer variables are not implicit roots.

Managed stores validate ownership of the destination slot and the stored
reference. Scoped borrows retain the owning allocation and expose a stable address
until the borrow ends. Raw pointers alone do not retain objects. Collection reclaims
unreachable cycles. Shutdown requires all root frames and borrows to have ended.

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

The compiler emits canonical Neri IR and transport 1.1. The transport header
includes versions, flags, payload size and a SHA-256 digest. The native reader
validates the envelope and the typed program before constructing LLVM objects.
Malformed, unsupported and incompatible inputs produce stable NIR diagnostics.

`neri-codegen` owns LLVM lowering and target emission. Native runtime symbols
and C imports remain explicit IR imports. Debug and Release preserve checks and
strict floating-point semantics. Reproducibility compares canonical NIR, native
objects and linked compiler executables independently.
