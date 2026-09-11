# Native interactive-session feasibility

This experiment runs two separately compiled Neri submissions in one native
process. Each submission loads the same independent source library through a
version-2 `neri.json` reference to the library's `state` unit. The first creates
a mutable object, an alias, and a generic container holding a closure. The
second observes and mutates that same object through both the alias and the
retained closure after collection.
The first module's normal application `main` remains uninvoked.

The Neri runner builds both Debug and Release modules and checks their process
output and status. A C loader supplies the native operations: dynamic function
pointer invocation and a GC root frame that outlives generated calls. It owns
one runtime context, checks each module's runtime requirements, roots the
returned state before collection, and destroys the heap before unloading code.
The final collection reports zero managed objects.

Run from a bootstrapped checkout:

```sh
NERI_ROOT="$PWD" scripts/neri.sh run \
  --project experiments/interactive/neri.json --unit probe
```

The runner retains generated projects, modules, compiler LLVM output and
per-configuration stdout/stderr/status files under the printed
`build/interactive-probe.*` directory. `NERI_AR` selects the LLVM archiver;
`NERI_PROBE_COMPILER` selects a freshly bootstrapped compiler for submissions.

For native loader/runtime instrumentation, supply an archive built with
`NERI_SANITIZERS=ON`:

```sh
NERI_ROOT="$PWD" NERI_PROBE_SANITIZE=1 \
  NERI_PROBE_RUNTIME="$PWD/build/interactive-sanitize/libneri-runtime.a" \
  scripts/neri.sh run --project experiments/interactive/neri.json --unit probe
```

This instruments the C loader and uses the supplied instrumented runtime;
generated submission code retains the compiler's ordinary Debug/Release
instrumentation. Validation covers macOS ARM64 with LLVM 22.1.8. The runner
also contains Linux linking options, whose execution remains unverified.

## Boundary

The loader accepts only this fixed, compiler-checked `State? -> State` fixture
entry. Its symbol is read from emitted LLVM; the state declaration is identical
in both compilations. This experiment establishes native lifetime feasibility
for that contract, rather than a general module compatibility protocol.

Descriptors, trace functions, static literals and closure methods reside in
loaded modules. Their code stays loaded until every corresponding managed
object and root is gone. A pointer alone does not retain a managed object.

The experiment exposes a two-submission native probe. It has no interactive
parser, persistent declaration registry, general evaluation API, custom
initializer, or terminal frontend. The delivery stages and remaining acceptance
belong to [issue #36](https://github.com/hakumi-dev/neri/issues/36).
