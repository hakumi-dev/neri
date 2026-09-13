# Agent feedback

`neri agent` exposes compiler feedback to a local coding agent through MCP over
standard input and output. It keeps project source buffers in memory, analyzes
them with Neri's parser and binder, and returns diagnostics and semantic context.
Project tools inspect saved sources across declared project members. The test
tool builds and runs explicitly registered test units.

The coding agent controls the improvement cycle: observe the project, edit its
sources, run the relevant checks and decide whether to retain the change.
Diagnostics establish syntax and type correctness; test results establish the
registered behavioral contracts; timings report the measured build workload.
Neri supplies these observations and their input identities. Scheduling work,
choosing an improvement objective and applying or reverting disk edits belong
to the agent host.

Start one server for one project unit. MCP clients use their own configuration
shape, but a generic server entry is:

```json
{
  "mcpServers": {
    "neri": {
      "command": "/absolute/path/to/neri/scripts/neri.sh",
      "args": ["agent", "--project", "/absolute/project/root", "--unit", "app"]
    }
  }
}
```

`--project` accepts an absolute project root or manifest path. `--unit` selects
the manifest unit and is optional when project selection is unambiguous. The
stdio transport uses newline-delimited UTF-8 JSON-RPC messages. Standard output
contains protocol messages only.

## Tools

`neri.open` opens an existing source from the selected unit or its references.
The optional `text` value supplies an unsaved overlay; otherwise Neri reads the
current file. A source can have only one open buffer.

`neri.inspect` takes an open source URI, an `expectedRevision`, and a UTF-16
position. It returns the current diagnostics and, when the compiler can recover
one unambiguous completion site, the expected type, receiver context, enclosing
callable identity, and completion candidates. Candidate edits use UTF-16 LSP
ranges. `insertText` and `textEdit.newText` contain the same actual insertion;
`cursorByteOffset` is the UTF-8 byte offset of the suggested cursor within that
text.

`neri.edit` applies one UTF-16 range replacement to an open in-memory buffer.
The edit is atomic after its arguments, bounds, workspace revision, and buffer
limits validate. It advances the buffer version and workspace revision, then
reanalyzes the source's owning compilation unit and its references. The result reports
`persisted: false` and `executed: false`.

`neri.feedback` takes an `operationId` and analyzes every unit in the root
manifest, declared `projects` members and their referenced libraries. Each unit
retains its own reference visibility and entry-point rules. A library change is
checked in every consumer's context, including consumers in other members.
Independent units remain separate. The result reports `scope: "project"`, a
snapshot, unit diagnostics, `complete`, `unitsTotal`, `unitsAnalyzed` and
`unitsReused`. `invalidatedUnits` identifies units whose input closure required
analysis; `removedUnits` identifies previously reported units absent now.

`neri.describe` takes `operationId` and a nonempty `query`. It searches bound
declarations across the project graph and returns signatures, inferred types,
documentation, identities, source URIs, UTF-16 ranges and the unit context.
Local variables and parameters stay in point inspection. The query is a
case-sensitive substring of declaration names, identities, signatures or types.
`omittedUnits` and `resultsTruncated` make incomplete coverage explicit. Results
are limited to 128 declarations and 64 KiB.

`neri.test` builds and executes units carrying [test metadata](PROJECTS.md)
in the root or declared member manifests. A referenced library alone does not
enroll its project's executable tests. Results include the phase, exit kind and
code, captured output, timeout failures, build snapshot and executable SHA-256.
`noTests: true` has `status: "unavailable"`. Temporary artifacts are removed
after execution. Output truncation is explicit. Test code has the host process's
filesystem and process capabilities.

`neri.profile` builds all graph units with release optimization and measures
compiler phases. Libraries produce object files and executable units produce
executables. Results contain the artifact digest, input fingerprint, normal or
abnormal exit and measured durations grouped by phase with sample counts.
Object-cache hit and miss events remain distinct phases. `build-complete`
measures elapsed build time; nested phases overlap, so their durations must not
be added to derive total time. Applications are not launched. A missing artifact
or incomplete timing stream produces an unavailable result. Builds have a
120-second limit per unit. This measures compiler work; application CPU, memory,
SQL and distributed tracing require runtime instrumentation.

The project tools use disk state independently of unsaved editor overlays. They
check the complete graph again before returning and report `conflict` when its
inputs changed. The CLI exposes the same services:

```sh
neri feedback --project .
neri describe --project . --query Customer
neri test --project .
neri profile --project .
```

Every tool call requires an `operationId`, which is echoed in its result. Tool
overlay results include the canonical source URI, workspace revision, document version,
analysis status, owning-unit `projectKey`, diagnostics, and effect flags. Invalid syntax and type errors
are normal analyzed results; protocol, range, stale-revision, membership, and
resource failures have distinct statuses. Rejected edits preserve the buffer.
If external files change during analysis, a conflict response explicitly states
whether the in-memory edit was already applied; the client uses the returned
revision to inspect again.

Analyzed overlay responses include a source excerpt of at most 4 KiB, its UTF-16 range,
and an explicit truncation flag. Inspection places the excerpt around the
requested position. Clients can inspect another position to obtain more text.

## Revisions and snapshots

The dependency model follows the separation of task inputs, dependency graphs
and rebuilding conditions studied in
[Build Systems à la Carte (Mokhov, Mitchell and Peyton Jones, 2018)](https://simon.peytonjones.org/assets/pdfs/build-systems-original.pdf).
Neri uses input digests and explicit member/reference edges; the integration
contracts verify consumer invalidation and independent-unit reuse. This is a
design application of that work, not a formal proof of the implementation.
Each tool publishes its own output schema through the
[MCP tools contract](https://modelcontextprotocol.io/specification/2025-11-25/server/tools).
Malformed arguments produce an error content item without a fabricated domain
result. Compiler syntax and type diagnostics remain normal analyzed results.

The **workspace revision** is the concurrency token accepted as
`expectedRevision`. It advances when an in-memory buffer changes and when the
server observes a change to project inputs, including manifests, closed source
files, generated-source inputs, and required standard-library sources. An edit
or inspection with a stale revision returns a conflict. Opening a buffer returns
the new revision to use for the next operation.

Standard-library observation follows its manifest and every source in imported
modules, including transitive imports from additional module files.

The **document version** counts accepted in-memory changes to one open buffer.
It is descriptive; workspace revision is the mutation guard exposed by the
tools.

The **analysis revision** identifies the internal LSP analysis generation.
Invalidation and reanalysis can advance it independently of workspace revision,
so clients use it to correlate feedback, not to authorize an edit.

The **overlay snapshot digest** is a SHA-256 digest of the analyzed project key and the
ordered source URIs and texts used by that analysis. Equal digests identify
equal values for those recorded fields; they do not compare every process or
toolchain setting. The digest does not state that external files remained
unchanged after the response. The server checks its workspace inputs again
before returning and reports a conflict if it observes a concurrent change.

The **project snapshot** hashes the complete unit inventory, canonical member
identities, each unit's observed inputs and the running compiler's binary digest.
It includes source ownership, generated-source contracts, project manifests and
the imported standard-library closure. Compiler replacement invalidates cached
results even when its version string stays the same. Profiling and test artifacts
have separate binary digests. Snapshots describe observations before and after
an operation; they are not filesystem transactions or locks.

Coverage follows explicit manifest membership. Nested projects enter the graph
through `projects`; directories of negative fixtures and bootstrap seed overlays
retain their own validation workflows. Seed overlays target the pinned compiler
syntax and are verified by the bootstrap build. The MCP feedback reports compiler
diagnostics; it does not subscribe to Rider's inspection panel.

The selected unit's reference closure defines which files can be opened. Each
source is analyzed in its owning unit's closure, preserving the same dependency
visibility as the editor. Inspecting a library source therefore does not report
errors in its consumers; inspect the consumer source to analyze that unit with
the current library overlays.

Diagnostics come from the parser or binder for that owning compilation unit,
not only the inspected file. Each diagnostic includes its source URI, UTF-16
range, code, severity, and message. Early project, generated-source, file-name,
and standard-library failures during analysis are retained as structured
diagnostics even when editor publication is disabled. Workspace setup failures
return `unavailable` with the project loader's message before document analysis.

## Semantic context and limits

Completion context is constructed from a temporary source copy containing a
completion marker. The copy is parsed and bound with the same project sources,
visibility rules, scopes, generic inference, and expected-type rules as editor
completion. Expected types include their semantic name and kind. Symbol metadata
distinguishes the compiler's `semanticIdentity` from a canonical `identity`
anchored to a declaration in the original source. `identityStatus` reports when
that mapping is canonical, absent, or unresolved. Recovery preserves a canonical
identity only where the query offsets can be mapped back to the original text.
Argument-name contexts identify the invoked callable; other expression contexts
identify the enclosing callable when one exists.

Limited delimiter repair supports some unfinished calls. The query
does not reuse an older semantic model after project setup or parsing becomes
unavailable.

Candidates are capped at 128 within 64 KiB, and `truncated` reports when a bound
is reached. Complete context is capped at 128 KiB; larger context returns
`status: "limit"` and an empty, explicitly truncated candidate list.
Diagnostics return at most 128 entries within 64 KiB and separately report their
total count and truncation.
Input is limited to 1 MiB per document, 8 MiB across open buffers, 64 open
buffers, 1,024 observed workspace paths, 16 MiB of workspace analysis input,
and 2 MiB per MCP message.

A completion candidate is a context-sensitive suggestion, not proof that the
insertion produces a complete, valid program. The final edited unit is parsed
and bound again, and its diagnostics are authoritative. Point inspection and
buffer edits operate on in-memory source snapshots. Test execution is an
explicit `neri.test` operation; the agent host owns disk edits and commits.

## Input contracts

Tool input contracts are declared in typed descriptors. Each descriptor supplies
its public name, description, input fields and decoder. The same field structure
produces the advertised JSON Schema and validates required fields, types,
unknown properties and duplicate keys. Decoding produces a typed request whose
payload contains the fields available to that operation. Workspace execution
consumes those requests; JSON parsing and tool-name lookup belong to the protocol
boundary. Shared input limits measure source text in UTF-8 bytes, while positions
and ranges use UTF-16 code units.

This applies the single-description approach used by
[MLIR's Operation Definition Specification](https://mlir.llvm.org/docs/DefiningDialects/Operations/):
declarative facts drive multiple consistent representations. Neri evaluates its
descriptors directly in Neri; the descriptors describe the agent protocol.

## Research basis

[SWE-agent: Agent-Computer Interfaces Enable Automated Software Engineering
(Yang et al., 2024)](https://arxiv.org/abs/2405.15793) shows that a purpose-built
agent-computer interface can materially affect software-engineering agent
performance. Its benchmark results motivate a compact structured interface;
they do not establish Neri's effectiveness.

[Hazelnut: A Bidirectionally Typed Structure Editor Calculus (Omar et al.,
2017)](https://www.cs.cmu.edu/~comar/hazelnut-popl17/) gives a mechanized account
of statically meaningful incomplete programs with typed holes. Neri's temporary
completion marker and delimiter repair do not implement Hazelnut's edit calculus
or inherit its soundness results.

[Synchromesh: Reliable Code Generation from Pre-trained Language Models
(Poesia et al., 2022)](https://arxiv.org/abs/2201.11227) demonstrates constrained
semantic decoding with language-specific completion engines. Neri exposes
semantic feedback after tool calls; it does not constrain a model's token
sampling, so Synchromesh's validity results do not transfer.

[Type-Constrained Code Generation with Language Models (Mündler et al.,
2025)](https://doi.org/10.1145/3729274) develops a sound prefix automaton for a
foundational typed calculus and evaluates a restricted TypeScript
implementation. Neri does not implement that automaton. Its guarantee is the
direct engineering contract that reported diagnostics and types come from the
current Neri parser and binder snapshot.

The wire protocol follows the official [MCP base
protocol](https://modelcontextprotocol.io/specification/2025-11-25/basic),
[tools](https://modelcontextprotocol.io/specification/2025-11-25/server/tools),
and [stdio transport](https://modelcontextprotocol.io/specification/2025-11-25/basic/transports)
contracts.

## Reproduce the protocol contract

From a built source checkout, run the Neri contract driver against that compiler:

```sh
neri_agent_work=$(mktemp -d)
scripts/neri.sh run --project . --unit agent-protocol-contracts -- \
  "$PWD" "$PWD/build/current/bin/neri" "$neri_agent_work"
```

The driver creates a temporary project, opens two source buffers, receives a
type error, applies a repair, and queries the inferred argument type in an
unfinished expression. It also checks revision conflicts after buffer and disk
changes, UTF-16 edit boundaries, the published result schema, and MCP lifecycle
and framing behavior. Source-buffer edits remain in memory throughout the
protocol conversation.

## Automatic feedback after agent tools

`neri feedback --project ROOT [--unit UNIT] [--operation ID]` analyzes saved
sources and writes one structured feedback result to standard output. It shares
the project graph with the MCP tool and uses the current parser and binder.
An explicit `--unit UNIT` selects the single-unit feedback contract; omitting it
selects the complete graph independently of `defaultUnit`.
The command exits successfully when it delivers feedback; consumers inspect
`status` and `diagnosticCount` to distinguish valid code, invalid code and an
unavailable analysis. Invalid CLI arguments exit with status 2.

`--codex-hook` reads a bounded JSON hook event from standard input and emits the
Codex `PostToolUse` response envelope. It correlates feedback with `tool_use_id`
and places the compiler result in `hookSpecificOutput.additionalContext`. The
adapter accepts a final JSON value terminated by either a newline or EOF. A
malformed event produces explicit unavailable feedback. Diagnostic messages and
source excerpts are identified as program data in the enclosing context.

Configure the project's `.codex/hooks.json`:

```json
{
  "hooks": {
    "PostToolUse": [{
      "matcher": "*",
      "hooks": [{
        "type": "command",
        "command": "neri feedback --project . --codex-hook",
        "timeout": 300,
        "statusMessage": "Analyzing Neri project"
      }]
    }]
  }
}
```

Set an absolute project path when tools can run from another directory. The
executable must be the built Neri version containing `feedback`.
Codex reviews and trusts hook definitions through `/hooks`; a configured hook
becomes active after the host accepts it. Neri supplies the feedback and Codex
incorporates it before the agent's next decision. The adapter responds at tool
completion boundaries, including completed editing commands.
Delivery follows the host's supported tool paths. Changes made by an external
editor are observed at the next invocation.
Codex's default hook context limit is approximately 2,500 tokens. Larger reports
arrive as a preview and a path to the complete saved output; the agent reads that
file for omitted unit diagnostics. The aggregate status, coverage and diagnostic
count precede the unit list. Host preview limits and Neri's explicit diagnostic
truncation are separate limits.

Optional `--state FILE` enables deduplication for hook invocations. The parent
directory must exist. State is a disposable cache: it records input identity and
the host session, turn and transcript identity. An unchanged invocation in that
scope emits `{}`. A new scope receives fresh feedback even for unchanged files.
Missing turn identity disables deduplication. The input identity includes
project and unit selection, compiler version and executable digest, the canonical standard-library
location, its manifest and required sources, and observed project inputs.
Project scope also records per-unit results.
When one input closure changes, unaffected units reuse their recorded results.
The response identifies reused units and keeps their truncation flags. Version 2
state records carry an integrity digest over scope, inputs and the report.
Project reports must satisfy their schema and correlated totals before reuse.
The digest detects accidental corruption; it does not authenticate a writer
with access to the state file.

State writes concern only this cache. `effects.persisted` describes source-file
persistence and remains false. A missing, partial or unwritable cache causes
repeat feedback. Only consistent `valid` or `invalid` analyses populate the
cache. Analysis checks its inputs again before returning; concurrent changes
produce `conflict`. Each result describes the observed snapshot, and subsequent
edits require another analysis. The hook analyzes the complete project graph by
default. Test execution and profiling are explicit tools; source analysis alone
does not assert that tests passed or that application performance was measured.

The adapter follows the official [Codex hook contract](https://learn.chatgpt.com/docs/hooks).
The separation of analysis and host delivery also permits other clients to use
MCP or the plain JSON command. MCP resource subscriptions are an optional
transport facility; [the MCP resource contract](https://modelcontextprotocol.io/specification/2025-11-25/server/resources)
leaves context incorporation to the host application.

[Reflexion (Shinn et al., 2023)](https://arxiv.org/abs/2303.11366) studies the use
of task feedback across agent attempts. Together with SWE-agent's interface
study, it motivates the edit/analyze/repair cycle. Protocol tests establish
Neri's feedback delivery and revision behavior; agent task-success improvements
require a separate evaluation.

The integration contract can be reproduced with:

```sh
neri_feedback_work=$(mktemp -d)
scripts/neri.sh run --project . --unit agent-feedback-contracts -- \
  "$PWD" "$PWD/build/current/bin/neri" "$neri_feedback_work"
```

It exercises disk errors and repairs, consumer diagnostics after dependency
changes, overlay isolation, hook delivery and deduplication, and changes in an
additional source of a standard-library module. The MCP protocol contract also
checks the published feedback schema and preservation of overlay revisions.
