---
id: query-session-types-without-runtime-frames
date: 2026-09-09
scope: feature
tags: [sessions, completion, semantics, performance]
source: bug-fix
confidence: 0.5
related: []
---

# Query session types without reconstructing runtime frames

## Context
Completion needs the types of committed bindings and the semantic context at the cursor.

## Mistake
Generating access expressions for every retained binding makes query analysis depend on the depth of the runtime frame chain.

## Lesson
Supply referenced bindings as typed temporary parameters in the query child model. Merge other visible names from binding metadata. Keep preparation and execution frame construction in the execution path.

## When to Apply
When adding semantic queries over persistent sessions, measure project size and submission history separately and verify that queries leave prepared submissions executable.
