# Testing Neri

`scripts/build.sh test` builds the current native codegen/runtime, reaches the
compiler fixed point with those artifacts, runs native ABI/IR tests and executes
language fixtures through the resulting compiler. `--debug` selects Debug native
components and Debug compilation of language fixtures. The compiler bootstrap
itself uses Release output for canonical generation comparisons.

`smoke-test` selects the documented run/argument contracts; `negative-test` selects
compiler diagnostics and runtime panics. Both commands first build a current
fixed-point compiler and preserve the published toolchain. `native-test` selects
the native probes. `test` (also named `check`) runs the full suite and publishes
only after every contract passes.

The hello and functions programs in `examples/` run as language cases. The package
gate additionally checks the documented arguments example through the installed
launcher. These checks keep the getting-started commands executable.
The run-cache sequence protects reuse, fresh arguments and exit status, source
and toolchain invalidation, optimization-mode separation, bypass and diagnostics.
It uses an isolated private cache and asserts behavior rather than timing.

Native probes call the exported C++ runtime directly from C and protect public
layouts, negotiation, root transfer, cycles, borrows and native allocation.
String probes cover embedded NULs, retention and numeric formatting; IR probes
cover malformed input and deterministic object emission for both native targets.
Their runtime checks remain active in Release. `NERI_SANITIZERS=ON` is a CMake
option for native address/undefined-behavior instrumentation.
`scripts/build.sh native-test --sanitize` builds and runs those instrumented probes.
On macOS this configuration links the shared LLVM library, keeping its uninstrumented
libc++ templates separate from instrumented Neri code. Mixed static templates can
produce [ASan container annotation false positives](https://github.com/google/sanitizers/wiki/AddressSanitizerContainerOverflow#false-positives).
Container-overflow checks remain enabled. LLVM itself is an external prebuilt library.

Language fixtures cover class construction/dispatch, array layouts, short-circuiting,
floating-point operations, unsafe storage, C imports and deterministic rejection
of invalid expressions. Successful executions assert output or use the test
library; rejection fixtures require a nonzero compiler status and diagnostic.
The native scalar contract uses an independent C shared library to verify widths,
signedness and memory stride. Its conversion/overflow cases run in both Debug and
Release and require arithmetic panics, not merely a failing process.
`callbacks.hk` covers contextual and named callbacks, indirect storage, nested
captures and invocation after explicit collection, including trailing `do` blocks
on functions, methods and constructors, typed/multiple parameters and nested blocks.
The effects fixture exercises a parameterless `do` callback. The inline-callback,
do-target and do-ending rejection fixtures protect the syntax boundary.
`legacy-console-error.hk` rejects the removed console import spelling; the
existing console contract exercises `console.print`, `println` and `read`.
`callback-errors.hk` protects
signature checking, complete returns and the mutable/raw-pointer capture boundary.
`postfix-if.hk` checks conditional actions, optional refinement and loop control;
its negative fixture checks unsupported modifier forms and inline block rejection.
HTTP process cases use a Neri client against both a run-mode example and a built
executable. They require an available loopback port 8080. The small C supervisor
starts both processes and bounds their lifetime; protocol assertions live in Neri.
The process helper provides EOF on stdin and enforces a deadline on the process
group. It records stdout, stderr and exit status separately under `build/work.*`.
Each language case has a distinct `case-N` prefix and a `.source` sidecar identifying
its fixture, so later cases preserve earlier diagnostic evidence.

## Linux native validation

The Release CI job runs `scripts/build.sh test`: it fetches the pinned Linux seed,
verifies the native compiler fixed point, and executes the full language and
native contract suites. Downloading the Actions seed candidate uses a repository
token with read-only Actions access. Debug and sanitizer jobs run the native
boundary probes without repeating the language bootstrap.

Linux x86-64 builds the native boundary directly with CMake and Clang/LLVM 22.1.8;
this gate is independent of the macOS bootstrap seed. The CI job uses Ubuntu 24.04
and runs Release, Debug, ASan/UBSan and ThreadSanitizer configurations. Required development packages
include zlib, zstd, libedit, libffi and libxml2 alongside `llvm-22-dev`.
The Linux sanitizer gate uses a native x86-64 runner with ASan shadow-memory
support; Release/Debug results under cross-architecture emulation cover those
configurations only.

```sh
cmake -S . -B build/linux-native -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang-22 -DCMAKE_CXX_COMPILER=clang++-22 \
  -DLLVM_DIR=/usr/lib/llvm-22/lib/cmake/llvm -DBUILD_TESTING=ON
cmake --build build/linux-native --parallel 2
ctest --test-dir build/linux-native --output-on-failure --no-tests=error
```

The direct probes validate the current Linux runtime and backend independently
of a bootstrap seed. The Release language gate uses the separately pinned Linux
seed described in [the Linux setup guide](../docs/LINUX.md).

The compatibility fixtures originate from `hakumi-dev/hakumi-lang-old` commit
`08124bc579c767a0cc7a56e3c490a5b81dc5fba4`. Their source behavior and ABI contracts
are exercised directly by the self-hosted toolchain; no reference interpreter is
required. Tooling orchestration is defined in `tooling/build.hk`.
