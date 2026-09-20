# Typed session preparation

A session prepares source against an immutable committed environment. A
`SessionSourceSnapshot` supplies declarations resolved by the ordinary v2 project
loader, including referenced units and standard-library dependencies. Snapshot
declarations are visible to submissions; preparation never calls an application
`main`.

Preparation parses the new declarations and statements with the language parser
and binds them against the committed semantic environment. Syntax and bound
arenas retain their parent through stable absolute node identities; new symbols,
generic specializations and diagnostics belong to the candidate. One binding
pass infers the submitted locals and final expression, and a second binds the
generated execution frame for that submission. Existing bodies remain bound.
Preparation does not execute application or submission code.

`SessionEnvironment.prepare(source)` returns a `PreparedSubmission` with status
`complete`, `incomplete`, or `invalid`. Missing closing syntax at end of input is
incomplete. Other syntax and type failures are invalid. Diagnostics point into
the submitted text; diagnostics concerning retained or generated declarations
use the start of the submission rather than exposing generated source positions.
Top-level `return`, including one nested in submitted control flow, is rejected
because it could bypass the generated state commit. Returns inside submitted
functions and closures retain their ordinary meaning.

Complete preparation exposes the generated analysis fragment, the next typed state
frame declaration, ordered binding metadata and `resultType`. A final expression
reports its resolved type; declarations, assignments and `Void` calls report
`Void`. Frame fields use the exact type
names resolved by the binder. Each frame retains its predecessor, allowing a
later execution adapter to preserve managed identity, aliases, concrete generic
values and captured closures without unchecked native casts.

Each retained binding also carries a generation-qualified semantic declaration
identity. Newly submitted nominal classes expose a stable `nominal/<full-name>`
identity and a canonical layout made from the resolved base and declared fields in
source order. A loader compares this layout before reusing a nominal identity;
generic runtime descriptor compatibility requires the execution-layer protocol
and is not inferred from descriptor pointer equality.

`commit(prepared)` accepts a complete, unused candidate for the current generation.
Each environment has an owner identity supplied by the session frontend and keeps
a private, bounded record of active candidates. Commit reads its private record,
so public diagnostic and generated-source views cannot alter committed state and
a candidate from another environment is rejected. It atomically adopts
declarations, binding metadata and the next frame. `discard`
consumes a candidate without changing the environment. Stale and repeated
operations fail. Invalid preparation never changes the generation or registry.
The environment accepts at most 1 MiB of submitted source, 1024 declarations and
1024 bindings.

Identifiers beginning with `__neri_session_` or `__NeriSessionState` are reserved
for generated entries, frame links and frame types and are rejected in submitted
source. The generated predecessor parameter therefore cannot shadow a user
binding.

`validateInitializer(snapshot: SessionSourceSnapshot, name: String,
bindingName: String = "service", imports: String = "")` requires a safe
zero-argument function with a non-`Void` result. It prepares the named binding and
the optional import-only source. Validation binds the captured project once and
binds the generated frame and entry against that model. Execution commits the
captured project together with the initialized binding. Project source selection
belongs to the compilation-project loader.

The candidate/commit model follows the dependency-aware snippet preparation
contract described by [JEP 222](https://openjdk.org/jeps/222), while Neri initially
rejects declaration replacement. Runtime module loading and root ownership are
separate session execution contracts.

## Completion

`SessionEnvironment.complete(source, cursorByteOffset)` analyzes the request
with the typed semantic rules and committed context used for preparation. It
does not create a candidate, execute source, change the generation, or retain
declarations. The cursor is a UTF-8 byte offset in `source`. An offset outside
the source or inside a multibyte UTF-8 sequence returns `invalid-position`;
source larger than 1 MiB returns `limit`.

`SessionCompletionResult` contains its observed `generation`, `status`,
`count`, `truncated`, and a linked `first` list of `SessionCompletionItem`
values. Each item has `name`, `insertText`, `category`, `detail`, and the
UTF-8-byte replacement range `replacementStart` and `replacementEnd`. The
range is half-open: replace bytes in `[replacementStart, replacementEnd)`.
`cursorByteOffset` positions the cursor within the inserted UTF-8 text: the new
source offset is `replacementStart + cursorByteOffset`. Functions and methods
insert `()` in call contexts; the cursor goes inside when there are parameters
and after the closing parenthesis otherwise. Existing call or generic suffixes
are preserved. Variables, types, and functions expected as values insert names.
Categories are `method`, `function`, `field`, `variable`, `type`, `namespace`,
`keyword`, or `symbol`. The result contains at most 128 items. `truncated`
reports that matching candidates were left out at that bound. `disposed` and
`unavailable` report that the environment cannot analyze the request; a complete
result may have no items.

`ExecutableSession.complete(source, cursorByteOffset)` exposes the same result
for native sessions. Its `completionGeneration()` is a monotonic revision:
every successful execution and every successful `reset()` advances it. A
completion result reports that revision. Preparing, completing, a failed
execution, and a failed reset leave it unchanged. The wrapped
`SessionEnvironment.currentGeneration()` is a separate state-frame generation;
reset creates a new environment whose frame generation starts again at zero.

A client may prepare a candidate, request completion, and then execute that
candidate. Completion does not consume or alter the candidate. A successful
`execute(prepared)` consumes and runs the candidate once; a second execution of
the same candidate fails. This preserves the single execution of a final value
expression while allowing completion between preparation and execution.

Completion analyzes the submitted text against the immutable project snapshot,
committed declarations, and visible session bindings. It proposes names, types,
members, namespaces, imported library members, keywords, and `use` targets when
the current syntactic position and semantic binding provide them. Local and
child-model names take precedence over parent names with the same spelling.
Generated session names are excluded. The service does not claim that its list
is a proof of every syntactically or semantically valid insertion.

Installed-toolchain `use` completion reads module names from
`stdlib/manifest.json`, without parsing unimported library sources. Legacy
toolchains without this manifest use `ARTIFACTS.sha256` entries under `stdlib/`.
Explicit imports resolve all sources of the selected library unit, including
transitive imports declared by any of its files. Captured source bytes retain
their relative paths for diagnostics and frozen session submissions.

The language server serializes the same typed candidates into LSP completion
items. It returns a normal array when the list is complete and an LSP
`CompletionList` with `isIncomplete: true` when it is truncated. This follows
the [Language Server Protocol completion
model](https://microsoft.github.io/language-server-protocol/specifications/specification-current/#textDocument_completion).
The shared semantic query has the same separation between language-aware
candidate construction and presentation used by Roslyn's
[CompletionService](https://github.com/dotnet/roslyn/blob/main/src/Features/Core/Portable/Completion/CompletionService.cs).
The stricter soundness and completeness goals studied in
[Language-parametric static semantic code completion](https://doi.org/10.1145/3527329)
are useful criteria, but are not guarantees made by this API.
Repairing a temporary copy with a completion marker uses the existing parser
and binder. This follows the separation of incomplete syntax and semantic
queries discussed in [Principled Syntactic Code Completion using
Placeholders](https://www.mathematik.uni-marburg.de/~seba/publications/completion-syntactic-placeholders.pdf).
Neri uses its existing recovery rules and does not implement that paper's
grammar-derived completion algorithm.

## Session module code generation

`neri build --session-module` is a dedicated compiler mode for prepared
execution source. The compiler selects the generated `__neri_session_entry`
from bound IR; source annotations cannot nominate an export. Generation zero
has no language parameter and later generations take exactly one managed frame.
Every generation returns its new managed frame.

The native module exports the unmangled C symbols `neri_session_module_v1` and
`neri_session_entry_v1`. The first is an accessor for versioned metadata. The
second has the uniform C ABI `neri_ref_v1 (neri_ref_v1)`: generation zero
requires a null argument and later generations require the preceding typed
frame. Other generated functions and the program ABI requirement record have
local linkage in this mode.

Metadata names the uniform entry and records canonical source and target type
identities. Managed class identities include the session module identity and
the semantic declaration identity, so frame generations and session owners do
not collide while cumulative declarations keep their identity. The metadata
layout table is emitted from the same LLVM lowering layouts used for allocation
and tracing. Each entry records kind, flags, ABI payload size and alignment, and
the complete inherited sequence of managed-reference trace offsets. Closure
environments and concrete generic class instantiations are ordinary managed
class entries. Compatibility compares these values and identities; it never
compares runtime descriptor pointers or discovers a mangled LLVM function.
The canonical layout fingerprint also records the base identity, inherited and
declared field identities, resolved field types and offsets, concrete generic
arguments, and dispatch slots with their implementation and complete callable
signatures. Two layouts with equal size and tracing remain incompatible when,
for example, an `Int` field is changed to `Float` under the same TypeId.

Session modules require runtime ABI 1.19 and
`NERI_RT_FEATURE_SESSION_MODULES` (`1 << 23`). The coordinator owns runtime
initialization and keeps the current frame in a GC root while invoking a later
module. On final reset it clears the root and collects before unloading modules.

`ExecutableSession` supplies the typed preparation/execution boundary. Its
native coordinator creates an opaque, process-unique owner identity and a
persistent runtime root slot. A load resolves only the two session exports and
validates the complete metadata header, entry address, source/target chain and
every bounded layout entry before invocation. Repeated TypeIds must have equal
kind, flags, size, alignment and trace offsets. A rejected module is closed
without changing the current state or committed preparation.

Accepted modules remain loaded while the persistent state can reach their type
descriptors or trace code. `reset` first clears the state slot and collects,
then unloads modules in reverse order; it does not shut down the process
runtime. The same `ExecutableSession` can prepare and initialize a fresh
generation zero after reset. Initializer execution is accepted once per reset.
Prepared candidates also carry a generated environment identity, preventing
two environments with the same display owner string from accepting each
other's candidates.
If unloading fails after the root has been cleared, the coordinator keeps only
the failed module handles and rejects execution until `reset` is retried. Its
frame TypeId and layout registry are cleared immediately, so a null state can
never be invoked as a later-generation frame. The typed environment advances to
a fresh generation only after the retry reports success.

Before compiling, `ExecutableSession.execute` privately claims a prepared
candidate and validates its owner, generation and active registry entry. The
compiler receives the immutable execution snapshot stored by preparation;
changes to the public status, generation or generated-source view cannot change
executed code. Failed compilation or linking releases the claim for retry and
removes its registered temporary directory. Native success is followed by the
matching private commit. The coordinator's environment and owner identity are
private so callers cannot advance the prepared schema independently of native
state.
Each candidate owns an immutable project snapshot and the complete transitive
standard-library import closure used during binding. Execution lowers its private
bound candidate directly to IR and invokes native code generation. Mutating the
caller's snapshot or `NERI_STDLIB` after preparation therefore cannot change
compiled code. Project, generated and
library sources retain separate parser boundaries, including the boundary before
the first captured library. Their aggregate snapshot is limited to 1 MiB.

`SessionToolchain(compiler, linker, target, workRoot, release = false,
cacheEnabled = true)` takes a `TargetPlatform` and selects Debug or Release
module compilation and code-cache use. `TargetPlatform.parse(name)` validates
external target names and returns `null` for an unsupported target. A project
initializer is loaded through the normal project manifest, validated as a safe
zero-argument non-`Void` function, and executed independently of the project's
`main`. `initializeProject(projectPath, unit, functionName, bindingName, imports)`
accepts an optional binding name (default `service`) and a string containing only
`use` declarations (default empty). The imports and named initializer result are
available in the first committed generation, using one compiled module. The
initializer still executes once per successful initialization. Names must be
single identifiers outside the reserved session namespace; executable statements
are rejected in `imports`. Reset discards the instance and allows initialization
of a fresh one. Existing three-argument callers keep the `service` binding.

`SessionTerminal` is a small frontend over this API. It accepts optional
context before the first prompt, collects multiline input through a line
containing only `.`, formats preparation and execution diagnostics, handles
`:reset`, and disposes the session on EOF. The `session-console` unit reads its
compiler, linker, target and work directory from the documented `NERI_SESSION_*`
environment variables.

## Installed session API

Release packages include a relocatable Neri project at
`share/neri/manifest.json`. Its `session` library unit exposes
`ExecutableSession`, `SessionToolchain`, the preparation and response types,
and `SessionTerminal`. A consumer declares an external project reference to
that manifest and unit; the reference remains relative to the consumer project,
so moving an installed toolchain does not bind it to the source checkout.

A consumer process configures the matching installed toolchain before creating a
session. With `toolchainRoot` denoting its immutable installation directory, the
environment is:

| Variable | Value |
|---|---|
| `NERI_HOST` | `toolchainRoot/libexec/neri-host` |
| `NERI_CODEGEN` | `toolchainRoot/bin/neri-codegen` |
| `NERI_RUNTIME_MANIFEST` | `toolchainRoot/lib/neri-runtime.json` |
| `NERI_STDLIB` | `toolchainRoot/stdlib` |
| `NERI_LIBRARY_PATH` | `toolchainRoot/lib` |
| `NERI_LINKER` | Absolute path to the pinned LLVM `clang++` |
| `SDKROOT` | Selected macOS SDK directory, on macOS |

Pass `toolchainRoot/libexec/neri` as `SessionToolchain.compiler` and the same
linker path as `SessionToolchain.linker`. The compiler path participates in cache
identity. Submission compilation invokes the configured native code generator
directly. `NERI_CACHE_DIR` optionally selects a private cache directory.
The packaged `examples/session-consumer` project demonstrates the library
reference and preparation/execution API.

The preparation and execution calls are separate:

```neri
let prepared = session.prepare("app.increment()\n")

if prepared.status.isComplete()
  let execution = session.execute(prepared)

  if execution.succeeded() && execution.resultType != "Void"
    console.println(execution.resultText)
  end
end
```

`prepared.resultType` is available before execution. Preparing the call does not
increment the application value. Executing the candidate invokes it once and
returns its already-computed display text. Executing that candidate again fails
the consumed-candidate contract.

The package also installs `bin/neri-session`. The launcher configures the
packaged compiler, runtime, standard library and target before starting the
session terminal. `NERI_SESSION_WORK` may select the parent directory for
temporary session modules; otherwise the platform temporary directory is used.
The terminal accepts empty lines inside a multiline submission and treats only
an input EOF as termination.
The terminal uses the ABI 1.21 optional line reader, so an empty submitted line
continues multiline collection while actual EOF releases the session. Successful
responses display a final non-`Void` expression result. Other successful responses
list each newly retained binding as `name: Type`, or report `ok` when there are no
new bindings.

## Retained native execution and results

The executable API uses IR transport 1.7 and runtime ABI 1.24. Each generation
emits its new functions and state frame. Earlier functions and class layouts are
typed retained declarations; the linker references their owning generations.
The session keeps that native code loaded until reset or disposal.

Neri application builds produce native executables. The interactive executor
uses the same typed compiler and emits native object files for new submissions.
On macOS arm64, the packaged object linker relocates that machine code directly
into the session process. Other hosts use shared modules. A code-cache hit loads
already compiled code.

The macOS executor discovers `libneri-session-object-linker.dylib` beside
`NERI_RUNTIME_MANIFEST`. `NERI_SESSION_OBJECT_LINKER` selects an explicit path;
an empty value selects shared-module loading. The choice is captured when the
`ExecutableSession` is constructed. Each object generation has its own symbol
table and resolves earlier code through that session's dependency generations.
The object bridge accepts compiled object files and performs no source or IR
compilation. Ordinary application executables do not load this bridge.
It registers unwind frames. Session debugging with LLDB uses the shared-module
backend (`NERI_SESSION_OBJECT_LINKER=""`), whose debug objects are registered by
the platform loader; the object backend does not register debugger objects.

Artifact identities hash a length-framed predecessor identity and the current
source fragment. The module exports `neri_session_module_v2_<identity-hex>` and
`neri_session_entry_v2_<identity-hex>`. The retained loader resolves these exact
names and checks the frame and layout contracts before invoking the entry.
Session ownership is a separate private identity, allowing identical immutable
code to serve independent sessions.

`execute(prepared)` returns `SessionExecutionResult` with `succeeded`, `status`,
`message`, `cacheHit`, `resultType` and `resultText`. A final value expression is
evaluated once into a typed temporary. Its display text is retained in the new
frame and copied into runtime-owned storage after execution. Returned text remains
valid after reset or disposal. Strings and numeric values display their values;
booleans display `true` or `false`; other values display a type summary, with
`null` for a null optional. Formatting invokes no user conversion or `toString`
method. Numeric formatting uses explicit generated casts and does not enable
implicit conversions in source programs. Public candidate metadata cannot change
the privately accepted expression or its result type.

## Code cache

The macOS arm64 code cache uses `NERI_CACHE_DIR`, or `~/.neri-run-cache` when that
variable is unset. Its session namespace is separate from executable run-cache
keys. Entries contain native code and a publication receipt. Runtime state and
expression results belong to the live session. A fresh session starts with empty
state; reset restores empty state. Each successful initialization executes the
initializer, including when its native code comes from the cache.

Keys cover canonical IR, source and project configuration identities, the
compiler, code generator, linker, runtime manifest and artifact, target,
optimization mode, SDK configuration, object-linker artifact when selected, and
retained generation dependencies. The object and shared-module backends use
separate cache namespaces. Frozen
source contents participate through the IR and predecessor identity. Artifact
receipts validate file identity, ownership, mode, size, mtime and ctime. Publication
uses a private staging directory and atomic rename after input fingerprints are
checked again. A miss or unavailable cache follows normal compilation.

The cache uses the existing Darwin file-identity boundary. Other host ABIs and
custom native-library dependencies use uncached compilation. `cacheEnabled =
false` also selects uncached compilation. A cache hit still prepares and checks
the candidate; opening a new project session analyzes its captured project.

## Cost model and references

Let `A` be the application, `d_i` the new submission, `Q_i` the retained type and
signature metadata, and `k_i` the retained module dependency count. The executable
path separates new-source analysis and body compilation from metadata lookup,
cache validation and linking:

```
T_i = analyze(d_i) + metadata(Q_i) + cache_check(i)
    + (cache_miss ? compile(d_i) + file_link(k_i) : 0)
    + load_and_relocate(k_i) + execute(d_i)
```

The object backend has no `file_link` step. A cache hit still loads and relocates
the cached object into a fresh session; it does not reuse application state.

Previously emitted function bodies remain in their owning generation. Let `a`
be the initial application's native body bytes, and suppose each submission
contributes `b` new body bytes after specialization. After `n` submissions, their
storage is `a + nb`. Duplicating all preceding bodies in every generation would
instead retain `(n + 1)a + bn(n + 1)/2` bytes. These counts exclude metadata and
state frames.
Symbol lookup, retained layouts, hashing and linking also cost time, so retaining
bodies does not imply constant-time latency. Benchmarks vary project size and
submission position separately and report preparation and compile/link/execution
timings.

The cache dependency contract follows the separation of scheduling and rebuilding
in [Build Systems à la Carte](https://simon.peytonjones.org/assets/pdfs/build-systems-original.pdf).
Persistent semantic ownership and stable identity requirements are also described
in the [Rust compiler's incremental compilation guide](https://rustc-dev-guide.rust-lang.org/queries/incremental-compilation-in-detail.html).
The separation between preparing code, loading it and obtaining a result is
consistent with [JShell's execution interface](https://docs.oracle.com/en/java/javase/25/docs/api/jdk.jshell/jdk/jshell/spi/ExecutionControl.html).
The native object loader follows LLVM's
[JITLink object-linking contract](https://llvm.org/docs/JITLink.html#jit-linking)
and [ORC resource ownership](https://llvm.org/docs/ORCv2.html#how-to-remove-code).

## Ownership and execution failures

The session owns its persistent GC root, loaded modules and generated temporary
files. `reset` releases that state, and `dispose` releases the coordinator itself.
Application services supplied by an initializer remain application-owned;
their external files, processes and sockets follow the services' explicit
ownership contracts. Resources acquired with `using` inside a submission follow
the normal scoped-resource rules. Owned resources cannot be retained in session
frame fields.

Preparation and compilation errors preserve the committed session state. A
fatal runtime panic terminates the host process and does not provide session
rollback or scoped cleanup. Native submissions execute in the host process
with its permissions.
