# Compiler boundaries

Neri programs obtain ordinary APIs from source declarations. A declaration in
the standard library determines its namespace, name, parameters, result type,
documentation, navigation, completion, and import requirement. The compiler
does not infer those APIs from runtime symbol names.

| Belongs to | Current rule | Reason |
| --- | --- | --- |
| Source library | Namespaces and callable signatures such as `console`, `math`, `host`, `test`, and `tasks`, plus contracts such as `Equality` | Source controls the user-facing API and editor information. |
| Intrinsic registry | Intrinsic ID, native symbol, exact signature, effects, ABI minor, runtime features, and parallel safety | Lowering must emit a verified ABI import. |
| Compiler and runtime | Primitive and structural types, managed String and Array layouts, GC roots, pointers, and native records | These determine memory layout and serialized IR. |
| Compiler | Literal types, explicit numeric casts, primitive operators, indexing, and the built-in array `Length` member | These operations belong to the current primitive and structural type surface. |
| Core library loader | Automatic `core.hk` loading and the registered `String` declaration | Every compilation and session starts with the same literal representation. |
| Compiler and runtime | Scoped task operation, callback proof, and disjoint result slots | The runtime relies on the compiler's parallel-safety proof. |
| Source contract checker | Contract signatures and structural implementation matching | Generic source code receives a static callable surface. |

The compiler implements these language rules and checks the declarations that
connect source libraries to native operations.

## Intrinsic ABI contracts

`@intrinsic("id")` identifies a compiler-recognized operation. The intrinsic
registry is authoritative for that identifier's native link symbol, exact
signature, effects, minimum runtime ABI minor version, required runtime
features, and parallel-safety property. The binder requires an intrinsic
declaration to match that contract exactly before lowering can emit an import.

This registry is an ABI safety boundary. It prevents a source declaration from
claiming an arbitrary native symbol or weaker effects. It does not define public
library names. Standard-library declarations such as `console.println`,
`math.max`, `host.readText`, and `test.assertTrue` select their intrinsic IDs in
source.

## Language representations

The compiler and runtime jointly own primitive scalar types, managed references,
arrays, optionals, raw pointers, native records, garbage-collection roots, and
their serialized IR types. `String` is declared by the core library and has the
registered UTF-8 representation; the runtime owns its object layout and literal
type. These facts are checked across the compiler/runtime ABI rather than
implemented by ordinary library methods.

Parallel task generation also crosses this boundary. Source declarations supply
its generic operation and user-facing signature. The compiler verifies callback
and capture requirements, emits the scoped-task IR operation, and the runtime
uses the verified array layout to write disjoint output slots.

This division follows the normal intrinsic boundary: LLVM requires intrinsic
functions to have known semantics and restrictions in its
[Language Reference](https://llvm.org/docs/LangRef.html#intrinsic-functions),
and recommends an intrinsic for an extension expressible as a function call in
its [extension guide](https://llvm.org/docs/ExtendingLLVM.html). Rust provides a
related example where compiler-recognized operations are supplied by marked
library items in its [lang-item documentation](https://rustc-dev-guide.rust-lang.org/lang-items.html).
