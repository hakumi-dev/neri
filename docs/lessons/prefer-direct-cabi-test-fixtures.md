---
id: prefer-direct-cabi-test-fixtures
date: 2026-09-07
scope: project
tags: [neri, cabi, testing, fixtures]
source: user-correction
confidence: 0.7
related: []
---

# Prefer direct C ABI calls for trivial test fixtures

## Contract
- Declare trivial platform functions directly with `@cabi` when Neri can express
  their exact parameter and return types.
- Keep C helpers for capabilities Neri cannot safely express, especially work
  constrained by post-`fork` async-signal-safety.

For example, POSIX `getpid` returns `Int32`, and `kill` accepts two `Int32`
arguments and returns `Int32` on the supported platforms. Neri fixtures declare
these signatures directly. The escaped-child fixture uses a native helper so
that the post-`fork` child calls only async-signal-safe functions before `_exit`.

## When to Apply
Apply when a Neri contract test proposes a native helper for a small libc or
POSIX operation such as process identity or signal delivery.
