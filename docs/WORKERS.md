# Isolated workers

Runtime ABI 1.32 provides a native pool of persistent workers with separate
managed heaps. Its declarations are in
[`runtime_abi.h`](../native/include/neri/runtime_abi.h), under
`NERI_RT_FEATURE_ISOLATED_WORKERS`. The `workers` standard-library module exposes
this boundary through typed results and an owned `Pool` resource. Its API and
error handling are written in Neri; the compiler generates private entry adapters.
ABI 1.33 adds readiness descriptors and multiplexed waiting under
`MULTIPLEXED_IO` (2147483648).

## Neri API

Import `workers` and call `workers::start(entry, config, options)`. The entry must
name a safe, nongeneric module function with the exact signature
`fn(Byte[]): Void`. Closures, variables containing functions, instance methods
and C ABI functions cannot be entries. The configuration is copied into each
worker's own `Byte[]`; application state and connections are created there.

`start` returns `result::Result<workers::Pool, result::Failure>`. Use `using` in
a function returning `resources::Outcome` to preserve startup, application and
cleanup failures. `Options` exposes `workers`, `maxOutstanding`, `maxInputBytes`,
`maxOutputBytes`, and `maxReservedBytes` with the limits listed below.

| Operation | Result |
| --- | --- |
| `pool.submit(bytes)` | `Result<Ticket, Failure>`; admission or a recoverable error such as `full` |
| `pool.poll(waitMilliseconds: 0)` | `Result<Poll, Failure>`; `Empty()`, `Stopped()`, or `Completed(completion)` |
| `unsafe pool.readinessHandle()` | `Result<Int, Failure>`; a borrowed descriptor for readiness polling |
| `pool.cancel(ticket)` / `pool.stop()` | `Result<Bool, Failure>` |
| `pool.close()` | Optional cleanup failure; closing an already closed pool succeeds |
| `workers::ready()` / `workers::reply(bytes)` | `Result<Bool, Failure>` |
| `workers::receive()` | `Result<Byte[]?, Failure>`; `null` means stop |
| `workers::cancelled()` | Cooperative cancellation flag |
| `workers::fail(detailBytes)` | Records a terminal diagnostic and returns `Result<Bool, Failure>` |

A completion has `ticket` and `outcome`. `Outcome` distinguishes
`Success(payload)`, `Failed()` and `Cancelled()`. Polling consumes the native
completion and releases its admission reservation. Tickets expose `id()` for
correlation and are tied to their pool; a different pool rejects cancellation
with `foreign_ticket`. Safe callers cannot manufacture pool ownership or tickets.

The entry calls `ready` after initialization, receives and handles messages, then
unwinds its resources before returning. `fail` records failure but does not
unwind or terminate the thread. See the executable
[worker contract fixture](../tests/runtime-contracts/workers/fixture.hk) and its
[separately compiled entries](../tests/runtime-contracts/workers/entries.hk).

The declaration `@operation("workers.start", startNative)` names its source
bridge explicitly. Binding converts only the entry argument to a private native
adapter; the bridge owns option validation, native calls and result construction.
The compiler validates the bridge signature and does not depend on the names or
layouts of `Pool`, `Options`, or `Result`. IR `worker.entry` requires transport
1.9 and the `isolated-workers-v1` feature.

Each worker initializes one runtime context, calls its entry, and shuts down
that context after the entry returns. Its roots, borrows, managed allocations
and native allocations belong to that context. It has no readable parent heap
and does not adopt allocations from another worker. This differs from joined
`tasks::generate`, whose parent is suspended until its tasks finish.

## Entries and ownership

The entry receives a copied configuration byte range. It constructs and owns
its application state inside the worker. It calls `worker_ready` once after
successful initialization, then receives and replies to one job at a time.
Resources can remain in lexical scopes around that loop; they must close before
the entry returns. Native entry adapters must restore their root and borrow
chains and return normally. Exceptions and `longjmp` across this boundary are
outside its contract.

No managed reference, closure environment or borrowed native resource is an
argument to the entry. Input and result mailboxes store native byte copies.
`worker_receive` claims a job and reports its length; `worker_read` copies its
bytes to caller storage. `worker_reply` copies a response and completes that job.
An oversized response is rejected without completing the job. A second receive
before replying and a reply without a job are invalid operations.

Pool handles belong to the creating native thread and active runtime heap.
Another thread, a nested task heap, or a later heap cannot operate that handle.
Joined task heaps cannot open pools. Worker-side mailbox operations also require
the entry's own heap; an inline nested task cannot consume its parent's job.
`worker_pool_close` stops and joins its workers, discards remaining results and
invalidates the handle. Heap shutdown also closes its owned pools before
reclaiming memory. Raw handles are not idempotent after close.

## Admission and results

`worker_pool_submit` copies input and allocates a monotonically increasing ticket
only when admission succeeds. Rejected jobs are never executed. Jobs are not
automatically retried: a retry could duplicate database or filesystem effects.

Admission reserves both a job slot and `input length + max_output_bytes` until
the completion is consumed. Queued jobs, active jobs and completed unread
results all count against these limits. A slow result consumer therefore causes
`FULL`, rather than an unbounded accumulation of responses. The byte budget
covers mailbox payload reservations; bounded queue metadata, configuration and
worker diagnostics are separate. It does not limit arbitrary allocations made
by application handlers.

`worker_pool_poll` peeks at the oldest completion, optionally waiting for one.
`worker_pool_take` copies and consumes a completed ticket only when the output
capacity is sufficient. A completion is successful, failed or cancelled. Failed
and cancelled completions have empty payloads. Empty successful responses are
distinguished by completion kind.

To wait for socket activity and worker results together, obtain
`pool.readinessHandle()` in an unsafe scope and register it for reading in an
[`http::PollSet`](HTTP.md#waiting-for-multiple-descriptors). The descriptor is
readable while completions are pending or the pool is stopped with no outstanding
jobs. Stopping alone leaves unfinished work unsignaled, so a coordinator can wait
for its completion without spinning. After a
notification, call `pool.poll(0)` to consume results without blocking. Readiness
is advisory: `poll(0)` may report `Empty()` after a notification. Consuming the
last completion drains the observed notification while work can still arrive.
A notification already in transit can arrive afterward; reconcile it
with another `poll(0)`, even when that poll reports `Empty()`. Drained stop remains
readable so the coordinator can observe shutdown.

The pool owns this descriptor. Callers may only poll it: never read, write or
close it. Remove it from the polling set before closing the pool, and never use
the descriptor after close. POSIX uses a pipe and Windows uses a loopback socket;
the descriptor has the same owning-thread and heap restrictions as the pool.

| Option | Accepted range |
| --- | ---: |
| Workers | 1–64 |
| Outstanding jobs | 1–65,536 |
| Maximum input bytes per job | 0–134,217,728 |
| Maximum output bytes per job | 0–134,217,728 |
| Reserved payload bytes | 0–1,073,741,824 |

Configuration is limited to 1 MiB. Each worker retains at most 4096 diagnostic
bytes. Callers select smaller budgets appropriate to the application; these are
hard upper limits, not recommended operating sizes.

## Cancellation and shutdown

Opening waits for every worker to announce readiness. A startup failure stops
and joins the partial pool before returning. Entries may observe cancellation
during initialization. An entry that exits unexpectedly fails its active job.
When the last operational worker fails or exits, queued jobs fail and new
submissions are rejected immediately, even while worker cleanup is still running.

Cancelling a queued ticket completes it as cancelled. Once claimed,
cancellation is cooperative: `worker_cancelled` exposes the request, and a later
reply completes as cancelled. Stopping the pool rejects admission, cancels
queued work, marks active work and wakes receivers. An entry should leave its
loop and finish resource cleanup when receive reports stop.

`worker_fail` records a bounded terminal diagnostic. The entry then unwinds its
resources and returns. Pool close reports a failed-worker count and the first
diagnostic, including a cleanup failure reported after a successful reply.
Startup failures report the same bounded information before discarding the pool.

Neither cancellation nor close kills a native thread. Initialization, rollback
and close can wait indefinitely for uncooperative code. An application requiring
a fatal shutdown deadline can use the process drain watchdog. Panic and explicit
process exit terminate the entire process; this pool is not crash isolation.

## Native services

Workers reject terminal, window and interrupt-lease operations before accessing
their process-wide state. A coordinator owns those services. Independent native
embedding threads must still serialize terminal/window lifecycle operations.
The interrupt lease synchronizes its state and requires its owner thread for
poll and release; the drain monitor can observe its pending flag separately.

Filesystem and network operations require exclusive ownership of each live
resource. Each database connection must remain with its owner and its driver
must support calls on independent connections from different threads. Separate
heaps do not make third-party FFI or process-wide library state thread-safe.
Console operations can interleave between workers; use coordinator messages for
ordered application logs.

## Design references

The distinction between private mutable state and shared immutable state follows
[Uniqueness and Reference Immutability for Safe Parallelism](https://www.microsoft.com/en-us/research/publication/uniqueness-and-reference-immutability-for-safe-parallelism/).
The heap boundary also draws on
[Disentanglement in Nested-Parallel Programs](https://www.cs.cmu.edu/~swestric/20/popl-disentangled.pdf),
without claiming its nested-task semantics or proof for persistent workers.
Copied messages between separate heaps follow the ownership model described in
[Erlang's process efficiency guide](https://www.erlang.org/doc/system/eff_guide_processes.html).
Bounded admission and backpressure follow the queue principles in
[SEDA](https://www.cs.princeton.edu/courses/archive/fall04/cos518/papers/seda.pdf);
this pool does not implement SEDA's adaptive stage controller.
