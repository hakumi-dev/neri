---
id: verify-early-child-stdin-close-on-macos
date: 2026-09-08
scope: module
tags: [processes, macos, pipes, regression-tests]
source: bug-fix
confidence: 0.8
related: []
---

# Exercise early stdin closure on each supported host

## Context
Supervised child input is delivered by a native writer while separate readers
drain stdout and stderr. A child may exit before consuming its input.

## Mistake
Blocking SIGPIPE only in the writer thread still allowed the macOS regression
to terminate the supervisor with exit 141 when the child exited early.

## Lesson
Configure `F_SETNOSIGPIPE` on the macOS parent pipe descriptor before starting
the writer. Keep a Neri regression that supplies more than pipe capacity to a
child that immediately exits, and require the supervisor to survive and dispose
the child. Verify this separately from the concurrent input/output test.

## When to Apply
Changes to supervised stdin delivery, pipe ownership or child cleanup.
