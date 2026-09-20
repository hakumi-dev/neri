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

## Result publication

Save baseline and candidate JSONL files and process-accounting output in a
separate ignored `build/` directory for each experiment. Include the exact
compiler digests, source revisions, hardware, commands and environment alongside
the samples. Compare matching workloads and report both distributions; record
latency targets with the issue or experiment that defines them.

Publish the evidence with its PR, issue or CI run according to the
[source and result policy](../README.md#source-and-result-policy).
