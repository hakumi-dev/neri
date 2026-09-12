# Agent feedback

`neri agent` exposes compiler feedback to a local coding agent through MCP over
standard input and output. It keeps project source buffers in memory, analyzes
them with Neri's parser and binder, and returns diagnostics and semantic context.
It does not write source files or execute submitted code.

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

Every tool call requires an `operationId`, which is echoed in its result. Tool
results include the canonical source URI, workspace revision, document version,
analysis status, owning-unit `projectKey`, diagnostics, and effect flags. Invalid syntax and type errors
are normal analyzed results; protocol, range, stale-revision, membership, and
resource failures have distinct statuses. Rejected edits preserve the buffer.
If external files change during analysis, a conflict response explicitly states
whether the in-memory edit was already applied; the client uses the returned
revision to inspect again.

Analyzed responses include a source excerpt of at most 4 KiB, its UTF-16 range,
and an explicit truncation flag. Inspection places the excerpt around the
requested position. Clients can inspect another position to obtain more text.

## Revisions and snapshots

The **workspace revision** is the concurrency token accepted as
`expectedRevision`. It advances when an in-memory buffer changes and when the
server observes a change to project inputs, including manifests, closed source
files, generated-source inputs, and required standard-library sources. An edit
or inspection with a stale revision returns a conflict. Opening a buffer returns
the new revision to use for the next operation.

The **document version** counts accepted in-memory changes to one open buffer.
It is descriptive; workspace revision is the mutation guard exposed by the
tools.

The **analysis revision** identifies the internal LSP analysis generation.
Invalidation and reanalysis can advance it independently of workspace revision,
so clients use it to correlate feedback, not to authorize an edit.

The **snapshot digest** is a SHA-256 digest of the analyzed project key and the
ordered source URIs and texts used by that analysis. Equal digests identify
equal values for those recorded fields; they do not compare every process or
toolchain setting. The digest does not state that external files remained
unchanged after the response. The server checks its workspace inputs again
before returning and reports a conflict if it observes a concurrent change.

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
and bound again, and its diagnostics are authoritative. The service does not
perform token-level constrained decoding, introduce typed holes, run tests, or
execute, save, or commit code.

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
