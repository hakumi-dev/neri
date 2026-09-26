# SQLite resource lifetime

The `contract` unit repeats 512 isolated in-memory connection lifetimes. Each
cycle performs a buffered read, a failed statement preparation, pre-cancelled
streaming, a successful stream, and two idempotent closes. A retained query and
provider alias must reject reads after close. Successful `sqlite3_close` on every
cycle verifies that no prepared statement remains outstanding on that connection.
Only one connection is open at a time in this contract.

The concurrency-tokens worker contract separately covers bounded pools,
worker-local sessions, cancellation between operations, and scoped cleanup. Its
heavy-query deadline case covers cancellation during execution. This repetition
contract does not establish an RSS bound, test forced process termination, or
claim absence of managed-heap leaks.

Build the `contract` unit from this manifest in Release and run the resulting
executable without arguments. It creates no database files and requires no
application configuration.
