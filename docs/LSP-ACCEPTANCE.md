# Language-service acceptance criteria

These criteria define observable language-service behavior independently of an
editor or client implementation. Supported capabilities and limitations are
documented in [Language server](LANGUAGE-SERVER.md). A criterion does not imply
that its capability is implemented; servers advertise only supported features.

## Semantic capabilities

| Capability | Acceptance criteria |
| --- | --- |
| Diagnostics | Parser and type errors agree with the CLI. Unsaved edits update diagnostics; correction and document close clear them. Ranges identify the relevant source text, including Unicode and incomplete input. |
| Project model | Explicit source membership and library resolution support cross-file declarations. Changes invalidate affected dependents without combining unrelated programs or fixtures. |
| Completion | Candidates respect receiver types, visibility, scopes, shadowing and generic substitutions. Resolved items provide accurate types and documentation. Incomplete expressions remain usable. |
| Signature help | The active overload and parameter match the call context, including nested calls, callbacks and generics. |
| Hover | Types and documentation come from the compiler model and the symbol under the cursor. Unresolved or ambiguous input produces no invented result. |
| Navigation | Declaration, definition, type definition and implementation targets use symbol identity and exact source ranges across compilation sources and libraries. |
| References and highlights | Results distinguish bindings with the same spelling and include applicable reads, writes and declarations. |
| Document and workspace symbols | Names, kinds, containers and locations reflect declarations, with stable filtering and no generated duplicates. |
| Rename | prepareRename rejects unsupported targets. Versioned multi-file edits preserve binding and detect collisions; comments, strings and unrelated names remain unchanged. |
| Code actions | Fixes and refactorings apply to their diagnostic or selection, support resolution where applicable, and produce edits validated by reanalysis. |
| Formatting | Document, range and on-type formatting are idempotent and preserve semantics. Incomplete input does not cause destructive edits. |
| Semantic tokens and inlay hints | Classification, modifiers and inferred information agree with resolved compiler types and symbols; updates remove obsolete presentation. |
| Structural presentation | Folding and selection ranges follow source structure. Document links resolve to relevant targets. Code lenses expose applicable actions or semantic information. |
| Call and type hierarchies | Edges reflect resolved calls, inheritance and implementation relationships, including generic substitutions and overrides where supported by the language. |

## Protocol and responsiveness

- Capability and position-encoding negotiation follow the supported LSP version.
- Sequential ranged edits use the negotiated encoding, including CRLF and
  non-BMP characters. Invalid batches do not partially change document state.
- Document versions and dependency invalidation prevent obsolete results from
  replacing current results.
- Cancellation, debounce and scheduling keep new edits and requests responsive
  during expensive analysis; cancellation does not corrupt cached models.
- Lifecycle, malformed requests, process termination and client reconnection
  have deterministic outcomes. Resource operations and refresh/progress messages
  are supported when required by an advertised capability.
- Latency distributions, memory use and sustained-edit behavior are measured
  on reproducible small and large workloads, with cold and warm runs. Performance
  budgets identify the workload, environment and measurement boundary.

## Client integration and delivery

- Compatible clients launch the selected compiler's server and synchronize the
  intended project documents. Trust policy prevents unauthorized tool execution.
- Real editor validation covers errors appearing and disappearing on unsaved
  edits, incomplete syntax, Unicode and process recovery. Server protocol tests,
  client build success and syntax coloring do not substitute for that validation.
- Run, Build and Check invoke the configured CLI with consistent source sets and
  working directories. Paths and arguments containing spaces, failures and stop
  behavior are covered by execution tests.
- Reproducible bootstrap and package validation cover the compiler, libraries
  and server. Installation is validated with an independent protocol client and
  each supported editor integration.

## Applicability

Domain-specific features, such as color presentation, require a language use
case; unsupported features are not advertised as empty handlers. Debugging,
test discovery, build orchestration and editor-specific refactorings are
separate services. LSP coverage alone does not imply equivalence to C# tooling
or ReSharper.
