# SQLite Data benchmark

This benchmark measures SQLite-backed Neri Data writes, bounded typed reads,
streaming reads, and provider close. It uses a disposable in-memory database and
the generated `Customer` model from `experiments/neri-data/generated-example`;
it does not access application data. Compilation and linking are outside the
reported operation times.

Build the `benchmark` unit from this manifest with the matching Neri toolchain,
then run the executable with `<host-label> [rows-per-write] [repetitions]`.
Defaults are 250 rows and 3 repetitions. Each write repetition inserts its row
count once; later reads and streams see all inserted rows. Per-repetition writes
are saved as one tracked change batch. Scale is limited to 1–1,000 rows per
write, 1–10 repetitions, and 10,000 total rows. Output is newline-delimited JSON
on stdout with elapsed milliseconds, integer rows per second when elapsed time is
nonzero, actual delivered rows, scale, and host label. Schema setup is excluded
from operation timings; provider close is reported separately.

SQLite allocator counters use `sqlite3_memory_used()` and
`sqlite3_memory_highwater()` when they return usable values. They describe the
process-global SQLite allocator, not resident memory or Neri-managed memory; a
zero/unavailable counter is emitted as `null`. The before/after used-byte delta
is an observation, not a leak assertion. The optional
`NERI_MEMORY_PROFILE=1` mode is recorded in output and should be measured in a
separate run from the unprofiled baseline because profiling changes memory
behavior. Compare results only with the same toolchain, target, host, and scale.
