---
id: measure-session-size-and-history-separately
date: 2026-09-08
scope: feature
tags: [sessions, performance, benchmarks, incremental-compilation]
source: user-correction
confidence: 0.7
related: []
---

# Measure application size and session history independently

## Context

A minimal session fixture took roughly 0.35 seconds per compiled submission,
while the Sumi application reproduced roughly two seconds for the same simple
operations. Growing the history alone explained only a smaller part of that gap.

## Mistake

A small fixture and an accumulated-history benchmark can miss repeated work
proportional to the application's source, dependencies and retained metadata.

## Lesson

Measure fixed submissions across application sizes, then vary history length
while holding the application constant. Separate preparation from native
compilation/linking/execution and identify actual code-cache hits. Compare
fingerprinted coherent toolchains and include the real consumer's project.
Successful code reuse must also be checked for metadata scans, serialization,
hashing and allocation that still grow with the full retained context.

Compare instrumented runs with an uninstrumented control. Keep report writing
outside measured phases and give each record bounded write cost: reading and
rewriting an accumulated report adds quadratic work and can falsely implicate
the runtime. Reconcile phase totals with the end-to-end measurement before
choosing a different execution backend.

## When to Apply

Use this matrix when changing session preparation, retained-code generation,
cache keys, or the execution backend. A faster small example is insufficient
evidence for scalable interactive latency.
