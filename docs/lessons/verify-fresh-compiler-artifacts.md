---
id: verify-fresh-compiler-artifacts
date: 2026-09-08
scope: project
tags: [neri, testing, compiler, artifacts]
source: bug-fix
confidence: 0.5
related: []
---

# Verify the artifact produced by the successful build

## Context

Compiler and LSP contracts execute native binaries produced from the current
Neri sources and a selected native runtime/codegen tuple.

## Mistake

A failed build can leave an earlier executable at the requested output path.
Running that file verifies the earlier artifact, even when the run succeeds.

## Lesson

- Give each verification build a fresh output path.
- Require a successful build exit status and the new executable's existence
  before running it.
- Record the compiler path, native tuple and separate build/run results.
- Assert the expected source transformation before adapting expected results
  to observed output; formatting must preserve the complete document's content.

## When to Apply

Use these checks for self-hosted compiler changes, concurrent agent work and
native contract executables that are rebuilt repeatedly.
