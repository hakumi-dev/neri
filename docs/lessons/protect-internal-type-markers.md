---
id: protect-internal-type-markers
date: 2026-09-08
scope: module
tags: [compiler, types, inference, diagnostics]
source: bug-fix
confidence: 0.5
related: []
---

# Protect internal type markers from source declarations

## Context
The semantic model represents types by names, including internal markers used
for error recovery and null literals.

## Mistake
A user declaration could reuse a marker name and reach special assignability
or inference branches. A successful source check could then produce invalid IR.

## Lesson
Keep internal type identities distinct from user-defined types. In a model that
uses string markers, enforce reserved names at every declaration entry point,
including generic parameters and native records. Represent polymorphic argument
relationships explicitly instead of bypassing checks with a wildcard marker.
Test both rejection of marker collisions and normal inference for ordinary names.

## When to Apply
When adding a type marker, declaration form, builtin function, or inference rule.
