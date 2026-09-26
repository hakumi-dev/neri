# Synchronous streaming and cancellation

`stream` visits decoded rows synchronously without collecting the result set or
adding entities to the context's identity map. It is available on `Query<T>`,
scalar and two-field projections, compositional `ShapeProjection<T, R>` results,
and inner/left joins. The visitor returns
`true` to continue and `false` to stop. Query filters, ordering, distinct stages,
offsets, and explicit limits execute in the database. An unbounded stream has
no implicit 1000-row limit; explicit `take` retains its existing validation.

For a generated context bound to a provider:

```neri
let options = new StreamOptions(timeoutMilliseconds: 5000)
let result = db.customers.where(active: true).stream(fn(customer: Customer): Bool
  consumeCustomer(customer)
  return true
end, options)
```

`stream(visitor, options)` selects the bound provider.
`streamWith(provider, visitor, options)` explicitly selects a provider for that
call. Join streams require both inputs to share the same provider instance,
unless `streamWith` supplies an override. Every terminal requires the provider's
`StreamingReads` capability and dispatches through its streaming operation.

## Projected and joined results

Projection streams select and decode the requested scalar columns before
calling the visitor. Compositional shapes validate every leaf in a row before
running their nested DTO materializers. Earlier successful deliveries remain
observable if a later row fails decoding. Projected reads do not decode the
source entity's unselected fields.

```neri
let customerName = quote do |customer: Customer|: String
  return customer.name
end
let result = select(db.customers, textScalar(), customerName).stream(fn(name: String): Bool
  consumeName(name)
  return true
end)
```

Join streams deliver `Joined<L, R>` or `LeftJoined<L, R>` pairs. A left join's
presence marker distinguishes an absent right entity from a present entity
containing nullable values. Input filters, distinct, ordering, limits, and
offsets retain their pre-join meaning. An input's limit is not an output limit:
several matching rows can multiply the joined pairs. An explicit
`take` on the join bounds its output, and joined output has no guaranteed order.

Streaming projections and joins can return more than 1000 rows when unbounded.
Their buffered `compile` and `all` terminals retain the explicit bound contract.
Every explicit `take` still requires a value from 1 through 1000, including when
another `take` has already narrowed the result.

## Results and resource lifetime

`Completed(count)` means the input ended. `Stopped(count)` means the visitor
requested an early stop. `Cancelled(count)` reports cooperative cancellation
or an expired deadline. `Failure(error, count)` preserves a typed query failure.
The query terminal counts successfully decoded rows passed to the visitor,
including the last row whose visitor returned `false`.
If that visitor also cancels its token, its explicit `false` result takes
precedence and the outcome is `Stopped`.

The SQLite provider owns the native statement for the entire call. It copies
each row before invoking the visitor and releases the statement before returning.
Visitors can retain their received entities, but that retention belongs to the
application. Streaming does not guarantee constant total process memory:
SQLite can allocate working storage for sorting, distinct, or other operations.

## Cancellation and ownership

`CancellationToken` is cooperative and confined to the calling thread. It can
be cancelled before execution or by a row visitor. It is not a cross-thread
signal. `StreamOptions` also accepts a timeout in monotonic milliseconds; each
execution calculates a fresh deadline. SQLite checks the deadline between rows
and through a native progress callback during query evaluation, including work
before the first result row. Progress callbacks are periodic, so a timeout is
not a strict real-time completion guarantee. Application visitor execution is
not preempted.
Timeouts must be between 1 and 86400000 milliseconds. Tokens remain cancelled
once requested; a fresh token is needed for an independent cancellable call.

While a SQLite stream is active, the provider rejects reentrant execution,
schema operations, transaction mutations, and close. Capability and transaction
state inspection remain available. Connections remain sequentially owned;
streaming does not make a connection safe for concurrent use. Visitors and
diagnostic observers must return normally.

Native cleanup disables the progress handler and finalizes the statement
before releasing callback storage and bound text buffers. Cancellation and
early stopping leave the provider available for subsequent operations.

This API performs synchronous database work. Asynchronous I/O, asynchronous
iteration, and cross-thread cancellation remain separate capabilities.

## Verification

```sh
scripts/neri.sh run --project experiments/neri-data --unit streaming-contract
scripts/neri.sh run --project experiments/neri-data --unit streaming-contract --release
scripts/neri.sh run --project experiments/neri-data --unit diagnostics-contract
scripts/neri.sh run --project experiments/neri-data/provider-contract --unit streaming
scripts/neri.sh run --project experiments/neri-data/provider-contract --unit streaming --release
```

The core contract exercises unbounded offset/distinct plans, explicit bounds,
providers that ignore a stop, decoding failure, cancellation before dispatch,
invalid timeouts, and simultaneous cancellation and visitor stop. Projection
and join contracts also exercise explicit provider overrides, invalid chained
bounds, and a malformed late shape leaf after a successful delivery. SQLite
integration checks generated consumers, untracked entity and join delivery,
projection streams exceeding 1000 rows, join multiplicity beyond a bounded input,
left-join absence, strict decoding, reentrancy rejection, native deadlines, and
connection reuse. These tests establish observable behavior, not a throughput
or memory benchmark.

## Verified primary references

The following references were checked on 2026-09-22. They motivate the design;
they do not establish correctness or performance of the Neri implementation.

- Goetz Graefe, [*Volcano—An Extensible and Parallel Query Evaluation System*](https://awoc.wolski.fi/dlib/parallel-query/Graefe94-Volcano.pdf),
  IEEE TKDE 6(1), 1994, pp. 124–125, [DOI](https://doi.org/10.1109/69.273032). Its iterator
  interface separates lifecycle and record delivery. Neri uses a scoped visitor
  over SQLite stepping; it does not implement Volcano's operator engine.
- Microsoft, [efficient querying: buffering and streaming](https://learn.microsoft.com/en-us/ef/core/performance/efficient-querying#buffering-and-streaming):
  distinguishes collecting a complete result set from delivering individual
  results. Neri avoids accumulating rows and tracking streamed entities.
- Microsoft, [asynchronous programming](https://learn.microsoft.com/en-us/ef/core/miscellaneous/async):
  asynchronous execution and provider cancellation support are separate
  concerns. This increment does not claim asynchronous execution.
- SQLite, [progress callbacks](https://sqlite.org/c3ref/progress_handler.html):
  a nonzero callback result interrupts evaluation, only one handler is active
  per connection, and the callback must not modify that connection. Neri's
  native callback reads a monotonic deadline and updates native status only.
- SQLite, [interrupt](https://sqlite.org/c3ref/interrupt.html),
  [finalize](https://sqlite.org/c3ref/finalize.html), and
  [parameter binding](https://sqlite.org/c3ref/bind_blob.html): interruption,
  statement ownership, and bound-buffer lifetimes require coordinated cleanup.
  Neri does not expose a cross-thread `sqlite3_interrupt` handle.
- SQLite, [statement stepping](https://sqlite.org/c3ref/step.html): each successful
  row step makes one result available, and subsequent stepping advances the
  statement. Projection and join terminals use this same adapter operation.
