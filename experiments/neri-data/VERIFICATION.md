# Verification record

Local audit, 2026-09-12, macOS ARM64. Base source revision:
`23fdfc7c733d6105cd1a6a7e6f47890b45573f6b`. The experiment is uncommitted.
Compiler launcher: `scripts/neri.sh`; binary SHA-256:
`08945cab0326d609c171e5290bd6f3d14f506606dcea92a55d67ed39f666aaa2`.

## Passed checks

- Library and sample compile. The sample executes successfully.
- Behavior assertions cover omission, false, explicit NULL, quoted text as a
  separate parameter, parameter order, query reuse, integer ordering, a typed
  single-field projection and the relationship descriptor.
- The verification executable passes against the actual `data.hk` source.
  It exercises standalone LSP completion, project-aware unsaved completion through
  the editor manifest, and retained-session completion with `use neri_data`.
- Boolean completion exposes `eq(value: Bool)` and excludes `gt` and `isNull`.
  Nullable text exposes `isNull`. Chained query completion retains the entity in
  its predicate and return types.
- The four semantic negatives below are rejected by CLI, LSP and session
  preparation. Session preparation does not execute or persist these submissions.
- Formatting checks pass for the implementation, sample and verification source.

| Invalid expression | CLI / LSP first diagnostic | Session first diagnostic |
| --- | --- | --- |
| `customerActive().eq(1)` | NR112: expected Bool, got Int | NR112 |
| `customers().where(orderId().eq(1))` | NR112: incompatible entity predicate | NR112 |
| `customerActive().gt(1)` | NR131: missing member | NR131 |
| `customerActve().eq(true)` | NR110: undefined function | NR104: unsupported call target |

The typo's session diagnostic is less specific than CLI/LSP and remains an
acceptance gap. The harness records this observed difference explicitly.

With the named-argument implementation in the working tree,
`customers().where(active: true)` parses and is rejected by semantic binding:
`NR270` for the unknown `active` label and `NR273` for the missing `predicate`.
The fixture declares `where(predicate: Predicate<T>)`; entity-specific equality
labels belong to the generated API tracked in #63. Reproduce negatives with:

```sh
scripts/neri.sh check --project experiments/neri-data/verification --unit invalid-wrong-bool
scripts/neri.sh check --project experiments/neri-data/verification --unit invalid-cross-entity
scripts/neri.sh check --project experiments/neri-data/verification --unit invalid-illegal-op
scripts/neri.sh check --project experiments/neri-data/verification --unit invalid-typo
scripts/neri.sh check --project experiments/neri-data/verification --unit invalid-named-argument
```

Each command is expected to exit 1.

## Editor observations and acceptance gaps

Rider 2026.2, Neri plugin `0.4.3-dev`, compiler setting
`/Users/kb714/Projects/neri/scripts/neri.sh` was inspected in the UI.
The separate `editor/probe.hk` consumer was opened and edited without saving:

```neri
use neri_data

def main(): Void
  let field = customerActive()
  field.
end
```

No completion popup appeared after Ctrl+Space, invoking the Basic completion
action, or restarting the Neri language server and retrying. The editor showed
the expected incomplete-source diagnostic. Its log also recorded two
`Invalid document range` responses during these edits. That is an observation,
not an established cause. The matching project-aware direct LSP probe passes;
the real-editor acceptance gate remains open. The original valid file was
restored and saved, and settings were closed without changes.

The standalone incomplete generic-member completion returns an accurate method
signature but lacks `data` for documentation resolution, despite the method's
`##` documentation. The harness reports that gap; documentation and provenance
are not claimed as passing.

The fixture has no generator, stale-output invalidation, entity-specific named
labels, database provider, row materializer or Ito integration. Result typing
currently ends at the inspectable `QueryPlan`. These boundaries and the selected
generation/provenance contract are tracked in [#61](https://github.com/hakumi-dev/neri/issues/61)
and [#63](https://github.com/hakumi-dev/neri/issues/63).
