# Executable session benchmark

## Semantic completion

The `completion` unit measures the shared session completion engine against
10, 100 or 500 retained functions and 0, 25 or 100 committed submissions.
It performs one untimed query before timing repeated name and member queries.
Every run checks candidate counts and truncation. Preparation builds the typed
context without executing native code; these timings exclude application
startup, compilation, terminal rendering and process startup.

```sh
neri build --project benchmarks/session/manifest.json --unit completion --release --output build/session-completion-benchmark
/usr/bin/time -lp ./build/session-completion-benchmark 500 100 100
```

Run with the matching installed `NERI_STDLIB` environment described in
[the session API](../../docs/SESSIONS.md#installed-session-api).
The JSON lines report aggregate milliseconds and iteration count. Divide the
aggregate by that count for mean query latency. Process maximum RSS includes
the retained context, preparation, runtime and both query loops. Run each
size/history combination sequentially without compiler builds in parallel.

## Executable submissions

This benchmark measures `ExecutableSession` initialization and four sequential
submissions, then a dedicated sequence of 25 submissions that grows retained
context one binding at a time. It records preparation separately from the
combined compile, link and execution phase exposed by `ExecutableSession.execute`.
Every repetition uses fresh sessions and the same deterministic values.
`first-repetition` and `later-repetition` describe process order only. The
benchmark does not clear filesystem or operating-system caches.

Build the benchmark against the repository session unit, then pass a revision
and a toolchain fingerprint as metadata:

```sh
scripts/neri.sh build --project benchmarks/session/manifest.json --unit benchmark --release --output build/session-benchmark
mkdir -p build/session-benchmark-work
export NERI_CACHE_DIR="$(mktemp -d "$PWD/build/session-cache.XXXXXX")"
export NERI_HOST="$PWD/build/native/native-release/neri-host"
export NERI_CODEGEN="$PWD/build/current/bin/neri-codegen"
export NERI_RUNTIME_MANIFEST="$PWD/build/current/lib/neri-runtime.json"
export NERI_LIBRARY_PATH="$PWD/build/current/lib"
export NERI_STDLIB="$PWD/build/current/stdlib"
export NERI_LINKER=/opt/homebrew/opt/llvm@22/bin/clang++
export SDKROOT="$(xcrun --show-sdk-path)"
./build/session-benchmark build/current/bin/neri "$NERI_LINKER" macos-arm64 build/session-benchmark-work benchmarks/session/fixture/manifest.json build/session-benchmark.csv build/session-benchmark.jsonl 6 "$(git rev-parse HEAD)" "$(uname -s)-$(uname -m)-$(sw_vers -productVersion)" "$(shasum -a 256 build/current/bin/neri build/current/bin/neri-codegen build/current/lib/neri-runtime.json | shasum -a 256 | cut -d ' ' -f 1)"
```

Four optional trailing arguments select another initializer project: unit,
initializer function, retained binding name and import-only source. For example,
an application initializer can use `web`, `consoleApp`, `app` and the literal
string `use sumi\nuse console\n`. The benchmark never calls the application
`main`.

Build and run the generator to create a source-size matrix under `build/`:

```sh
scripts/neri.sh build --project benchmarks/session/manifest.json --unit generator --release --output build/session-project-generator
./build/session-project-generator build
```

The inventory lists fixtures with 10, 100, 500 and 900 ordinary functions,
one ordinary class per ten functions, and their exact source byte counts. Pass
the inventory's function and byte columns as the benchmark's final two optional
arguments. The first fresh session uses `compile-cache-prime-let-variable`; later
fresh sessions use `compile-cache-reuse-candidate-let-variable`. The latter is a
stable repeated input key, not proof of a cache hit; compare backend cache
telemetry or artifacts before classifying it as a hit. Execution rows include
the backend's `cache_hit` result; preparation rows leave that column empty.

Pass `profile-initializer` as the final optional argument to record three
read-only diagnostic phases after each complete initialization: project loading,
snapshot construction and initializer validation. These phases use a separate
`SessionEnvironment`; they explain work performed by `initializeProject` but are
independent repeated measurements and must not be added to the initializer row.

The initializer row contains its combined preparation, compilation, linking and
execution latency because `initializeProject` performs those stages as one API
operation. Accumulated-context rows record every position from 1 through 25, so
positions 1, 10 and 25 can be compared without hiding intermediate behavior.
Millisecond resolution, machine load and filesystem cache state are part of the
observation; the records define no pass/fail timing threshold.

The command uses a fresh private code-cache directory, so the first occurrence
of an artifact measures a code-cache miss and later identical inputs can reuse
it. This does not make the operating-system filesystem cache cold. Run timing
measurements without concurrent compiler builds. Keep optional stage metrics in
a separate run and compare its wall time with the uninstrumented run before
using those metrics to attribute latency.

## Application initializers

To measure an application instead of the fixture, pass its project, unit,
initializer, binding and imports using the optional arguments:

```sh
./build/session-benchmark \
  build/current/bin/neri "$NERI_LINKER" macos-arm64 \
  "$WORK" /path/to/application/manifest.json \
  "$CSV" "$JSONL" 3 "$REVISION" "$ENVIRONMENT" "$FINGERPRINT" \
  web consoleApp app $'use sumi\nuse console\n'
```

The names and imports in this example must match the selected application.
Use a different empty `NERI_CACHE_DIR` and work directory for every workload.
Leave `NERI_SESSION_OBJECT_LINKER` unset for sibling discovery of
`libneri-session-object-linker.dylib`. For uninstrumented runs, leave
`NERI_SESSION_METRICS` unset. Record the compiler and driver build modes separately
from the submitted modules, which use the default Debug session toolchain.

Store CSV, JSONL and identity metadata together under `build/`. Publish them using
the [source and result policy](../README.md#source-and-result-policy).
