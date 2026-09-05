# Architecture

Neri has three explicit implementation layers.

## Compiler

`compiler/` is the production compiler and is written entirely in Neri:

- `frontend/` tokenizes and parses source files;
- `semantic/` binds symbols, types, control flow, and diagnostics;
- `ir/` lowers bound programs to canonical Neri IR and provides the command-line driver.

Generic declarations, specialization, and inference live in separate semantic
components. `GenericInference` owns the type-variable bindings for one application.
The binder registers templates before resolving signatures, then processes newly
specialized bodies until the work set is complete. Specialization clones syntax
at type positions and preserves source locations. Concrete classes retain the
existing field layout and precise tracing rules; concrete functions use the same
IR and runtime ABI as ordinary functions. Specializations are cached by qualified
declaration name and canonical type arguments, with bounded expansion.

Function types are structural language types. Closure conversion represents each
signature with a hidden managed class and virtual invocation slot, and each
closure with an implementation class containing its captures. Existing class
allocation, tracing and indirect dispatch provide closure lifetime and invocation;
the IR transport and runtime ABI remain unchanged. The signature class is not
source-constructible, and its fallback invocation traps. A callee's `noreturn`
property does not propagate to callers or other implementations of its slot.

`compiler/ir/main.hk` is the executable entry point. `frontend/main.hk` and `semantic/main.hk` are standalone development entry points and are excluded from the compiler source set.

## Native boundary

`native/codegen/` is a narrow C++ LLVM consumer. It accepts only verified Neri IR and emits LLVM IR, assembly, or native objects.

`native/runtime/` implements the runtime and precise tracing garbage collector in
C++. Generated programs link to its static archive through the versioned
[runtime ABI](ABI.md).

Language syntax, binding, type rules, diagnostics, and Neri IR lowering are owned by `compiler/`.

## Build driver

`tooling/build.hk` owns native build orchestration, compiler source discovery,
staged bootstrap comparisons, and native/language test selection. The seed compiles
it before it builds the current compiler. CMake builds the native targets.
`native/tools/host.cpp` provides sorted file discovery and subprocess capture with
EOF stdin, separate output channels and a process-group timeout. It contains no
compiler source list, build graph or language expectations.

## Trusted seed

The bootstrap release contains a Neri compiler plus its matching codegen and runtime. It compiles the current sources, and the resulting compiler performs the next verified generation.
