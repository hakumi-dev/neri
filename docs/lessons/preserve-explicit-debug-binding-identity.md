---
id: preserve-explicit-debug-binding-identity
date: 2026-09-08
scope: module
tags: [compiler, debugging, ir, dwarf]
source: bug-fix
confidence: 0.5
related: []
---

# Preserve binding identity and lexical scopes through IR

## Context
Source variables can have several SSA values after assignments and control-flow joins.
Debuggers must distinguish these updates from declarations that shadow the same name.

## Mistake
Reconstructing lexical membership from source-span containment fails for transformed
nodes and synthetic adapters. Emitting a new debugger variable for every SSA value
can leave a stale value visible even when source breakpoints resolve correctly.

## Lesson
Carry explicit lexical scope identities and stable declaration locations through IR.
Reuse one debugger variable for each source binding, including updates at block joins.
Validate this behavior through a real debugger with separate checks before an
assignment, inside a shadowing scope, and after returning to the outer scope.

## When to Apply
When changing lowering, SSA construction, source locations, or debug metadata.
