---
id: isolate-parser-source-boundaries
date: 2026-09-09
scope: module
tags: [parser, lsp, diagnostics, prelude]
source: bug-fix
confidence: 0.5
related: []
---

# Preserve source boundaries during parser recovery

## Context
The compiler, LSP and sessions parse source collections with registered file
boundaries. A mandatory prelude follows incomplete editor input.

## Mistake
Resetting the namespace at the top-level boundary alone allows nested recovery
to consume the next source and attribute user errors to the standard library.

## Lesson
Treat each boundary as EOF in nested parsing and token advancement. Resume the
next source only from the program loop. Anchor missing-token and missing-newline
diagnostics on the preceding source. Test incomplete calls, declarations and
expressions followed by an intact annotated library declaration.

## When to Apply
Changes to prelude loading, multi-file parsing, session preparation or editor
error recovery. Verify both parser isolation and the LSP protocol response order.
