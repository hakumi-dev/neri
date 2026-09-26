# Runtime and IR boundary

The canonical exported declarations and layouts are in
[`runtime_abi.h`](../native/include/neri/runtime_abi.h). Runtime ABI 1.33 uses a
C calling convention on macOS ARM64, Linux x86-64 and Windows x86-64. Generated programs negotiate
major version, minimum minor version and required feature bits before execution.

Checked C entry uses `neri_rt_v1_foreign_enter` and
`neri_rt_v1_foreign_leave`. Each stack token records heap ownership and the
outer root/borrow chains. Entries preserve initialized host heaps and reclaim
their own temporary heaps on return. Explicit shutdown requires completed
entries. [C interoperability](C-INTEROP.md) defines the public export, native
function-pointer, thread, lifetime, and failure contracts.

`neri_rt_v1_host_executable_path` belongs to `BOOTSTRAP_HOST`.
`host::executablePath()` returns the canonical current process image path or
`null`. It uses the operating system's process image API independently of user
arguments: [dyld on macOS](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man3/dyld.3.html),
[`/proc/self/exe` on Linux](https://www.man7.org/linux/man-pages/man5/proc_pid_exe.5.html),
and [GetModuleFileNameW on Windows](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulefilenamew).
The package manifest also identifies the toolchain version and native target.

Compiler cache metadata requires ABI 1.28. `neri_rt_v1_cache_supported` reports
whether the current host supports persistent compiler caching: macOS ARM64 does;
Linux and Windows use the normal build path. `neri_rt_v1_cache_metadata` returns
normalized file kind, permission bits, real-user ownership and a 32-byte SHA-256
fingerprint. The fingerprint covers device, inode, mode, owner, group, modification
and change timestamps, and size, using fixed-width little-endian fields. Access
time and platform structure padding are excluded. The `follow` flag selects
`stat` or `lstat`; symbolic links remain distinguishable when it is false.
Invalid arguments, unsupported hosts and filesystem failures return `-1`.
Metadata is a snapshot and retains no handle. The compiler requires owned `0700`
cache directories and owned regular executable files before accepting a hit.
`neri_rt_v1_stderr_write_bytes` writes and flushes an explicit byte range to
standard error, returning `0` on complete success and `-1` on failure.

`neri_rt_v1_file_wait_readable(fd, milliseconds)` waits without consuming input.
It returns `1` for readable input or EOF, `0` for timeout, `-2` for interruption
and `-1` for failure. The timeout is a nonnegative millisecond count. POSIX uses
[`poll`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/poll.html);
Windows supports file and pipe descriptors with
[`PeekNamedPipe`](https://learn.microsoft.com/en-us/windows/win32/api/namedpipeapi/nf-namedpipeapi-peeknamedpipe).

`String` is declared in `stdlib/core.hk` with the registered `utf8` representation.
Its semantic methods lower to direct functions with an explicit IR `string`
receiver. The runtime string header, UTF-8 payload, tracing and literal allocation
retain their existing ABI. Represented classes use their registered storage;
ordinary classes use class descriptors and method dispatch slots.

The compiler's typed intrinsic registry maps empty-body module declarations to
reviewed runtime exports, with exact parameter/result types, effects and ABI
requirements. String primitives use this mechanism. Native managed-reference
calls use runtime imports; the unmanaged C ABI import contract remains separate.

The `INTERACTIVE_IO` feature (8192) provides generation-scoped terminal
leases, byte input with bounded waiting, terminal dimensions, and monotonic
milliseconds. Platform terminal layouts remain inside the runtime. Key decoding
and the public session API are implemented in Neri.

The `WALL_CLOCK` feature (131072) provides signed Unix epoch milliseconds through
a status and output pointer. It is separate from monotonic elapsed time and
requires ABI 1.13. UTC formatting and injectable clocks are implemented in Neri.

The `CRYPTO` feature (262144) requires ABI 1.14 and provides SHA-256 through
maintained platform libraries and bounded operating-system entropy. Linux
programs that use this feature link to OpenSSL's `libcrypto`.

The `ROOTED_FILES` feature (524288) requires ABI 1.15 and provides descriptor-relative
opens with symlink rejection. Neri validates complete relative paths, bounds reads,
and preserves primary and close failures.

The `PROCESS` feature (1048576), introduced in ABI 1.16, owns child execution
domains and bounded binary output capture behind generation tokens. The
`DIRECTORY` feature (2097152), introduced in ABI 1.17, enumerates entries relative
to an open directory and preserves operating-system and close errors.

The `SOCKET_CLOSE_RESULT` feature (4194304), introduced in ABI 1.18, reports socket
close failure. The `DRAIN` feature (16777216), introduced in ABI 1.20, provides a
native watchdog for explicitly configured whole-process termination after stop.
The `SESSION_MODULES` feature (8388608), introduced in ABI 1.19, provides the
versioned session metadata and coordinator bridge. The
`OPTIONAL_CONSOLE_READ` feature (33554432), introduced in ABI 1.21, adds
`console::readLine(): String?`: EOF before any byte returns none, while a blank
line returns an empty string. The existing `console::read()` contract is unchanged.

ABI 1.22 adds `PROCESS_IO` (67108864) for initial child stdin, terminal capture
and controlled interruption, and `FILESYSTEM_MUTATION` (134217728) for temporary
directories, filesystem mutations and metadata. Consumers negotiate these bits
when importing the corresponding services. Platform-specific availability is
reported by each operation.
`SOCKET_ENDPOINTS` (268435456) adds a bounded loopback TCP connection
operation for clients sharing one deadline across connect, write and read,
and bound-port discovery for listeners allocated with port zero.

ABI 1.23 extends session modules with exact artifact-specific entry lookup and
the retained frame's display value. The coordinator resolves the named artifact
export directly, keeps its dependency modules loaded, and copies the display
string into runtime-owned storage while the frame remains rooted. The returned
string remains valid after module unloading.

ABI 1.23 also provides `neri_rt_v1_host_canonical_path` under `BOOTSTRAP_HOST`.
It accepts a NUL-terminated UTF-8 path, an output byte pointer and its capacity.
It returns the UTF-8 byte length of the weakly canonical path, or `-1` on failure.
A capacity greater than that length receives the bytes and a terminating NUL;
a smaller capacity, including zero, queries the length and leaves the output
untouched. The project loader uses this filesystem boundary directly.

ABI 1.24 provides `neri_rt_v1_session_load_execute_object(handle, object_path,
artifact_identity, linker_path)` under `SESSION_MODULES`. The three path and
identity arguments are managed strings; the result is a session status code.
The coordinator loads the native object-linker bridge lazily, resolves the
artifact-specific exports, and validates the existing frame and layout contract
before invocation. Each coordinator owns its linked generations and releases
them after clearing its state root and collecting during reset.

ABI 1.30 adds `neri_rt_v1_session_load_execute_object_libraries` with a fifth
managed string containing newline-separated `@library` names. Before object
materialization, the session linker loads each shared library and exposes its
symbols to ORC. A missing or invalid library fails the submission before it
executes, while earlier session generations remain available. The existing
four-argument entry point remains supported. Session object linking requires
dynamic libraries; static-only native archives are not supported. Loaded native
libraries remain available for the host process lifetime; restart the host to
replace a native dependency.

`MULTIPLEXED_IO` (2147483648), introduced in ABI 1.33, adds
`neri_rt_v1_net_poll_many(descriptors, interests, events, count, timeout_ms)` and
`neri_rt_v1_worker_pool_readiness(pool, out_descriptor)`. Polling accepts 0–4096
descriptors in one OS wait and a timeout of -1 or 0–60000 milliseconds. Interests
use bits 1 for read and 2 for write; output bits are 1 read, 2 write, 4 error,
8 hangup and 16 invalid. Zero interest observes exceptional conditions only.
The return is a count of nonzero event slots, zero on timeout/interruption, or
-1 on failure. Valid output spans are cleared before input validation; invalid
counts provide no output guarantee. Output storage must not overlap inputs.
Failures retain the OS error through cleanup; success and interruption preserve
the caller's previous error value. POSIX accepts pipes and sockets. Windows uses
`WSAPoll` and may fail if all sockets are invalid; an empty set is a timed wait.

Worker readiness returns the existing worker status codes and a borrowed,
poll-only descriptor. Readability indicates pending completions or a stopped
pool with no outstanding jobs, but is advisory: callers must consult
`worker_pool_poll` without blocking. The runtime
maintains the signal as completions are consumed. Callers must never read, write
or close the descriptor, and its lifetime ends when the owning pool closes.
The Neri wrappers are [`http::PollSet`](HTTP.md#waiting-for-multiple-descriptors)
and [`workers::Pool.readinessHandle`](WORKERS.md#admission-and-results).

## Representation

`ISOLATED_WORKERS` (1073741824), introduced in ABI 1.32, provides persistent
workers with independent heaps and bounded byte mailboxes. Pool handles remain
in their creating thread and heap. [Isolated workers](WORKERS.md) specifies the
native entry, admission, cancellation and cleanup contracts. This feature does
not enable shared-heap `MULTIPLE_MUTATORS` or change joined task capture rules.

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
calls that can collect, including foreign calls. Native local pointer variables
are not implicit roots.

The backend omits a function's root frame when its verified transitive effects
exclude managed allocation, native allocation, safepoints and unsafe operations.
With one mutator per active heap, such a function cannot trigger collection of
its local values. Functions with those effects retain their frames. This follows
the [safepoint-rooting requirement](https://llvm.org/docs/GarbageCollection.html#identifying-gc-roots-on-the-stack);
Neri uses its explicit root stack, not LLVM statepoints.

Managed stores validate ownership of the destination slot and the stored
reference. Scoped borrows retain the owning allocation and expose a stable address
until the borrow ends. Raw pointers alone do not retain objects. Collection reclaims
unreachable cycles. Shutdown requires all root frames and borrows to have ended.

Mark bits are clear between collections. The collector completes reachability
tracing before sweeping its allocation list, reclaiming unmarked objects and
clearing survivors in that same sweep. This is the mark-and-sweep discipline
described in [McCarthy (1960), section 4c](https://www-formal.stanford.edu/jmc/recursive.pdf).
Collection makes one full allocation-list traversal. For N owned allocations,
R root slots and E scanned reference edges, local tracing and sweeping take
O(N + R + E) work. Validating ancestor references additionally walks the task
ancestry. Child collectors leave suspended ancestor objects and their mark bits
untouched.

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
1.4 for `scoped-tasks-v1`, 1.5 for `session-module-v1`, 1.6 for
`debug-scopes-v1`, 1.7 for `retained-modules-v1`, 1.8 for `c-interop-v1`,
and 1.9 for `isolated-workers-v1`.
The `native-libraries-v1` feature carries a library
name after each import's source location; empty names retain platform-default
symbol resolution. Only C ABI imports may declare a library. The transport header
includes versions, flags, payload size and a SHA-256 digest. The native reader
validates the envelope and the typed program before constructing LLVM objects.
Malformed, unsupported and incompatible inputs produce stable NIR diagnostics.

`call.virtual` (30) uses `[receiver, arguments...]` for a safe dispatch slot and
`[unsafe capability, receiver, arguments...]` for an unsafe slot. Every
implementation of a slot must agree on its unsafe contract. Unsafe calls require
the `UNSAFE` effect. The capability is verifier metadata; LLVM dispatch tables
and physical function signatures contain only the receiver and arguments.
Default-argument adapters preserve this contract at both call boundaries.

`c-interop-v1` carries each function's C export name after its retained flag
when present and before its entry block. Type tag 22 carries a C function
pointer: parameter count, result type, and parameter types. Its nullable form
uses the null pointer representation. `cabi.address` (62) selects a declared
C import or export; `call.cabi.indirect` (63) consumes an unsafe capability,
a typed function pointer, and exactly matching arguments. C calls conservatively
carry every may-effect, including collection and native allocation, and exclude
the unconditional no-return effect. Exports use checked runtime entry wrappers.

`isolated-workers-v1` adds `worker.entry` (65), with no operands and one direct
function symbol. The target is a safe managed module function taking exactly
`Byte[]` and returning `Void`. The result is a C function pointer taking
`Byte*` and `UInt64` and returning `Void`. Its private adapter copies native
configuration bytes into the current worker heap, roots that array for the
entry invocation, and returns normally. Runtime worker initialization and
shutdown surround the adapter call; no parent managed reference is captured.

`retained-modules-v1` marks class shapes and function signatures whose storage
and bodies are owned by an earlier immutable session module. Retained functions
carry no blocks, values, or debug state. Native lowering emits external typed
declarations for retained functions and class descriptors and emits definitions
only for the current module.

`debug-scopes-v1` records an ordered scope vector after each function's blocks.
Each scope contains a positive ID, its parent ID (zero denotes the function),
and a source location. Parent scopes precede their children.
Each debug local then carries its name, SSA value, scope ID and declaration
location. Successive values of one variable retain its declaration identity;
variables with the same name in nested scopes have separate identities.
Instructions and terminators carry the scope ID active when they are lowered.
The native backend maps these IDs directly to DWARF lexical blocks and updates
debug values at definitions and control-flow joins. Source spans provide
diagnostic locations; scope IDs determine lexical membership. This metadata
does not change the executable runtime ABI.

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
