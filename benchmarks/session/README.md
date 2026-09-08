# Executable session benchmark

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

## Final session measurements

The final 2026-09-09 macOS ARM64 observation used fixed-point compiler
`build/current/bin/neri` (SHA-256
`bdd82ed4b44244ca47b8f0d5b7e199727ec81d9d7df1d4250080cf52aa30e652`)
and benchmark driver `build/session-benchmark-final-abi24` (SHA-256
`465173b67a1951e80a2c26582430ef88741b811b92603b6a04d5b985c8aaa2e0`).
The packaged Sumi project selected unit `web`, initializer `consoleApp`, binding
`app`, and imports `use sumi\nuse console\n`.
The driver is a Release executable. Its `SessionToolchain` uses the default
`release = false`, so the measured session modules are compiled in Debug mode.

The Sumi records are `build/session-sumi-20260909-final-abi24-object.csv` and
`.jsonl`, with fingerprint
`902271ae79d100174e2b4ab8048254cae5cac0de0df45cfe3a7571f345c8afba`.
Their file hashes are `771db0df1f2656db22edb974da474015dc178cd8dbe83aa15476ea1d8ec8dc2a`
and `1e8a618f48be762ee22417aff76124feb2df68224920c8a70ae6840c285cc18f`.
The generated matrix records use the prefix
`build/session-size-20260909-final-abi24-object-size-` followed by 10, 100,
500 or 900 and the `.csv` or `.jsonl` extension. Each file embeds its function
count, source byte count and the same toolchain fingerprint.

Reproduce either run with the main command above, changing the compiler, driver,
project and optional arguments as follows:

```sh
./build/session-benchmark-final-abi24 \
  build/current/bin/neri "$NERI_LINKER" macos-arm64 \
  "$WORK" /path/to/sumi-demo/.neri/ito/manifest.json \
  "$CSV" "$JSONL" 3 "$REVISION" "$ENVIRONMENT" "$FINGERPRINT" \
  web consoleApp app $'use sumi\nuse console\n'
```

Use a different empty `NERI_CACHE_DIR` and work directory for every workload.
Leave `NERI_SESSION_OBJECT_LINKER` unset for sibling discovery of
`libneri-session-object-linker.dylib`. These are uninstrumented runs: leave
`NERI_SESSION_METRICS` unset.
