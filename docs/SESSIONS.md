# Typed session preparation

A session prepares source against an immutable committed environment. A
`SessionSourceSnapshot` supplies declarations resolved by the ordinary v2 project
loader, including referenced units and standard-library dependencies. Snapshot
declarations are visible to submissions; preparation never calls an application
`main`.

Preparation
parses declarations and statements with the language parser, binds a generated
entry with the ordinary semantic binder, and records inferred binding types from
the bound entry scope. It does not execute application or submission code.

`SessionEnvironment.prepare(source)` returns a `PreparedSubmission` with status
`complete`, `incomplete`, or `invalid`. Missing closing syntax at end of input is
incomplete. Other syntax and type failures are invalid. Diagnostics point into
the submitted text; diagnostics concerning retained or generated declarations
use the start of the submission rather than exposing generated source positions.
Top-level `return`, including one nested in submitted control flow, is rejected
because it could bypass the generated state commit. Returns inside submitted
functions and closures retain their ordinary meaning.

Complete preparation exposes the generated analysis source, the next typed state
frame declaration, and ordered binding metadata. Frame fields use the exact type
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

`validateInitializer(source, name)` requires a safe zero-argument function with
a non-`Void` result and prepares its result as the `service` binding. Validation
does not invoke the initializer or an ordinary application `main`. Project source
selection remains the compilation-project loader's responsibility.

The candidate/commit model follows the dependency-aware snippet preparation
contract described by [JEP 222](https://openjdk.org/jeps/222), while Neri initially
rejects declaration replacement. Runtime module loading and root ownership are
separate session execution contracts.

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
Each candidate owns a deep copy of its project files and the complete transitive
standard-library import closure used during binding. Execution recreates those
files under the module directory and points the child compiler at the captured
standard-library directory. Mutating the caller's snapshot or `NERI_STDLIB`
after preparation therefore cannot change compiled code. Project, generated and
library sources retain separate parser boundaries, including the boundary before
the first captured library. Their aggregate snapshot is limited to 1 MiB.

`SessionToolchain` selects Debug or Release module compilation. A project
initializer is loaded through the normal project manifest, validated as a safe
zero-argument non-`Void` function, and executed independently of the project's
`main`. `SessionTerminal` is a small frontend over this API. It accepts optional
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

The package also installs `bin/neri-session`. The launcher configures the
packaged compiler, runtime, standard library and target before starting the
session terminal. `NERI_SESSION_WORK` may select the parent directory for
temporary session modules; otherwise the platform temporary directory is used.
The terminal accepts empty lines inside a multiline submission and treats only
an input EOF as termination.
The terminal uses the ABI 1.21 optional line reader, so an empty submitted line
continues multiline collection while actual EOF releases the session. Successful
responses list each newly retained binding as `name: Type`; declaration-only and
statement-only submissions report `ok`.

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
