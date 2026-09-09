---
id: preserve-library-type-invariants
date: 2026-09-09
scope: project
tags: [neri, types, abi, constructors]
source: implementation-audit
confidence: 0.8
related: []
---

# Preserve invariants across every construction and call path

Library types rely on constructor visibility and validated factories. Explicit
base calls, implicit base calls and construction through intermediate classes
must all enforce access and execute the required initialization. Checking only
the immediate constructor leaves an ancestral bypass when lowering initializes
fields without invoking constructors.

Operator and conversion syntax resolves to ordinary bound instance calls.
Apply receiver capabilities and operand types before creating those calls;
helper APIs such as `test.assertEqual` must use the same operation. Identity
casts preserve identity, while readonly conversion methods can produce fresh
values without exposing mutable access to their source.

Managed values cross the runtime ABI, with its allocation and tracing contract.
A matching native symbol name does not make a `String` or array signature a
portable C ABI import. Keep the native verifier's boundary intact and reuse the
existing typed runtime services.
