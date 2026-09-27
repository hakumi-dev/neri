# SQLite resource lifetime

The `contract` unit repeats 512 connection lifetimes over one temporary SQLite
database containing 64 text rows. Each cycle materializes all 64 rows through a
bounded read, checks a failed statement preparation, cancels streaming after the
first materialized row, completes another stream, and closes twice idempotently.
Both complete reads verify the row count and text values. A retained query and
provider alias must reject reads after close. Successful `sqlite3_close` on every
cycle verifies that no prepared statement remains outstanding on that connection.
Only one connection is open at a time in this contract.

The cycles run in eight blocks of 64. After each block returns, the contract
collects the current managed heap and reads its runtime allocation counters.
The first block establishes a warm baseline; the remaining blocks require equal
managed object counts, managed byte counts, and runtime-tracked native bytes.
Measurement uses stack storage and scalar values, retaining no managed objects.

The concurrency-tokens worker contract separately covers bounded pools,
worker-local sessions, cancellation between operations, and scoped cleanup. Its
heavy-query deadline case covers cancellation during execution. This repetition
contract does not establish an RSS bound or test forced process termination.
The counters cover the current Neri heap and its runtime-tracked native
allocations, not all SQLite or libc allocations. Equality after collection
establishes stable retained allocations for this workload, not universal absence
of leaks.

Build the `contract` unit from this manifest in Release and run the resulting
executable without arguments. It creates its database in a temporary directory,
verifies directory cleanup, and requires no application configuration.
