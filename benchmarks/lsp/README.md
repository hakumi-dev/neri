# Semantic completion over stdio

`completion.hk` measures an actual `neri lsp` child process. Each response must
contain `active` with semantic `Bool` detail. The benchmark writes JSON Lines:
every sample, initialization latency, current server RSS, and nearest-rank
median/p95 summaries. Keep the raw output when comparing compiler builds.

The native transport supports 64-bit Darwin. Its `poll` declaration follows the
Darwin SDK (`nfds_t` is `unsigned int`); it is not a portable Linux declaration.
The child receives preallocated arguments and invokes only native descriptor,
exec, and exit functions between `fork` and `execv`. All child processes are
reaped, including failure paths. Protocol errors, timeouts, missing semantic
items, and error diagnostics in the unrelated document fail the run.

## Run

Build with a configured Neri installation, including its host, runtime manifest,
code generator, and linker environment:

```sh
neri build --project benchmarks/lsp/manifest.json --release --output build/lsp-completion-benchmark
export NERI_STDLIB="$PWD/stdlib"
export NERI_TEMPLATE_CATALOG="$PWD/share/neri/templates/declarations.json"

# Small field and named-argument queries, with an independent small unit.
build/lsp-completion-benchmark "$NERI_COMPILER" \
  "$PWD/benchmarks/lsp/fixture" "$PWD/benchmarks/lsp/fixture/data.hk" \
  10 10 "$PWD/benchmarks/lsp/fixture/unrelated.hk" synthetic

# Generated Neri Data field-pack arguments, in the editor workspace.
build/lsp-completion-benchmark "$NERI_COMPILER" \
  "$PWD/experiments/neri-data/editor" "$PWD/experiments/neri-data/editor/probe.hk" \
  10 10 '' data

# Same query in the repository, with edits to an unrelated compiler unit.
/usr/bin/time -l build/lsp-completion-benchmark "$NERI_COMPILER" \
  "$PWD" "$PWD/experiments/neri-data/editor/probe.hk" \
  10 10 "$PWD/compiler/frontend/lexer.hk" data \
  > build/lsp-completion.jsonl 2> build/lsp-completion.time.txt
```

`NERI_COMPILER` is an absolute executable path. Run compared builds against the
same immutable source tree, stdlib, template catalog, environment, and machine.
Keep `NERI_HOST` and `NERI_RUNTIME_MANIFEST` exported while running: compiler
project references require the configured platform assets during analysis.
Record compiler and corpus revisions/digests, CPU, memory, OS, client build mode,
wall time, and concurrent workloads. The benchmark never edits source files.

## Measurement contract

| Scenario | Request sequence |
| --- | --- |
| `cold` | New server, initialize, didOpen, immediate completion. |
| `warm` | Repeated completion at the same position/version in that server. |
| `unrelated-queued` | Wait for unrelated version 1 diagnostics; send version 2 change, then immediate completion in the original document. |
| `unrelated-delay-200ms` | Wait for version 2 diagnostics; send version 3 change, wait 200 ms, then completion. |

The delayed case records an injection delay, not proof that a particular worker
is executing. Diagnostics observed before the response are counted. Scheduling,
analysis, IPC, framing, and client JSON decoding are inside request wall time;
completion validation and result output are outside it. The monotonic clock has
millisecond resolution. Cold means a fresh server, not a cold filesystem cache.

The `data` profile overlays `where(active: true)` as `where()` in the existing
editor probe and queries immediately after `(`. The generated API, downstream
`.take(20)`, and remaining source stay intact. The synthetic profile appends a
small class and function to its document, then queries a field and a missing
named argument. Positions are converted from UTF-8 byte offsets to LSP UTF-16.

`serverRssKiBText` preserves `/bin/ps` output for the server PID after queries.
It is a current RSS observation, not a peak. `/usr/bin/time -l` is supplemental
process-accounting output; do not relabel its memory fields as server peak RSS.
The 120-second request deadline makes stalled runs fail rather than silently
discarding slow samples. Sample counts are process repetitions; warm requests
within one process are correlated and should not be treated as independent
process samples.

## Issue 72 baseline

Measured on an Apple M4 Pro (14 CPU cores, 24 GiB RAM), Darwin 27.0.0 arm64.
Each row uses 10 fresh servers and 10 warm queries per server. These are local
development-workstation observations with filesystem caches warm; background
desktop activity was not controlled. Times are milliseconds, shown as
median / p95. With 10 process samples, nearest-rank p95 equals the maximum.

| Corpus/query | Cold | Warm | Unrelated queued | Unrelated +200 ms |
| --- | --- | --- | --- | --- |
| Small field | 8 / 9 | 2 / 3 | 6 / 9 | 2 / 2 |
| Small named argument | 9 / 12 | 2 / 4 | 9 / 14 | 5 / 8 |
| Data editor, `where` | 140 / 149 | 62 / 74 | — | — |
| Data in repo, unrelated lexer | 138 / 153 | 63 / 75 | 112 / 123 | 1801 / 1880 |

The Data editor process RSS observations ranged from 78,640 to 79,216 KiB.
Opening and editing the compiler lexer raised that range to 629,824–643,696 KiB.
Total command wall times were 11.50 s (both small queries), 8.33 s (Data editor),
and 66.13 s (repository). The delayed repository result shows substantial
head-of-line blocking under this request sequence; it does not identify an
internal phase by itself.

Reproduction identity:

- Server SHA-256: `fe271ba657faf04027bca005386b87e3a7a0f4cf81756840099d3177ef06c924`.
- Release client SHA-256: `b24eb8f8f78fcf3d943123e1fb4396526533851d7cc11cb9c7305dccf6b953ba`.
- Data/repository corpus: Git archive of `ec6e357feae1372d1bd1bb3abad00ab147de0588`.
- Client compiled against that archived `compiler-core`; the tracked manifest
  references the current checkout for normal builds. All six benchmark Neri
  sources passed `neri format --check`; the Release client build succeeded.
- `NERI_HOST` and runtime manifest came from the configured macOS arm64 native
  Release build. `NERI_STDLIB` and `NERI_TEMPLATE_CATALOG` pointed to this
  completion worktree. Both variables and native assets were identical across
  the three runs.

Raw samples: [small](results/baseline-small.jsonl),
[Data editor](results/baseline-data.jsonl),
[repository](results/baseline-repository.jsonl). Supplemental `/usr/bin/time -l`
output: [small](results/baseline-small.time.txt),
[Data editor](results/baseline-data.time.txt),
[repository](results/baseline-repository.time.txt).

## Cooperative scheduling and scoped query results

The candidate uses the same Release client, archived Data/repository corpus,
stdlib, template catalog, native assets, hardware and 10-by-10 sample counts as
the baseline. Desktop activity was not controlled. Each completion validates
the same semantic `active: Bool` candidate. Times are median / p95 milliseconds.

| Corpus/query | Cold | Warm | Unrelated queued | Unrelated +200 ms |
| --- | --- | --- | --- | --- |
| Small field | 8 / 11 | 2 / 3 | 8 / 11 | 3 / 4 |
| Small named argument | 11 / 12 | 3 / 5 | 12 / 13 | 5 / 5 |
| Data editor, `where` | 91 / 98 | 52 / 57 | — | — |
| Data in repo, unrelated lexer | 86 / 91 | 52 / 57 | 78 / 87 | 61 / 74 |

The measured acceptance targets are p95 at most 160 ms cold, 75 ms warm, and
200 ms in the delayed unrelated-unit scenario. All Data measurements meet
these targets. The delayed repository median falls from 1,801 to 61 ms and
p95 from 1,880 to 74 ms. These observations describe this workload, not a
worst-case deadline for every program. The small workloads remain below 15 ms
p95 in every scenario.

Current server RSS observations range from 43,248 to 43,840 KiB for the Data
editor and 597,296 to 603,296 KiB with the unrelated compiler unit. Total command
wall times are 11.81 s for both small queries, 6.22 s for the Data editor, and
48.97 s for the repository. These are the same observation and accounting
methods used above, rather than measurements of peak server RSS.

The server registers declarations and binds the enclosing completion scope;
it does not cache a partially checked model as full diagnostics. Cooperative
checkpoints allow pending interactive messages to interrupt idle diagnostics.
Source collection imports each source once instead of repeatedly scanning the
accumulating source text for library imports. Reuse is restricted to verified
document and dependency inputs; full diagnostics still analyze the whole unit.

Candidate server SHA-256:
`4ef866e18c2c5e38330be58c85dfac4b8f8d96eaacead78b1bad4f5c22bc71ed`.
The compiler passed the Stage 1–3 IR, object and executable fixed-point checks
and the complete native and language contract suite. The benchmark ran before
the full validation suite.

Raw samples: [small](results/candidate-small.jsonl),
[Data editor](results/candidate-data.jsonl),
[repository](results/candidate-repository.jsonl). Supplemental process-accounting
output: [small](results/candidate-small.time.txt),
[Data editor](results/candidate-data.time.txt),
[repository](results/candidate-repository.time.txt).
