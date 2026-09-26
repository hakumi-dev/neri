# Typed error propagation

`try expression` evaluates an operation once, extracts its success payload, or
returns its failure from the enclosing callable. The callable's return type must
accept the propagated error. Ordinary successful returns still construct their
declared result explicitly.

```neri
use result

def increment(input: Result<Int, String>): Result<Int, String>
  let value = try input
  return Result<Int, String>.Ok(value + 1)
end
```

`match` handles an outcome locally. `try` passes its error to the caller. Neither
form discards an error, converts failure into null, nor asserts that an operation
must succeed.

## Expressions and boundaries

The operand includes calls and member access, with the same precedence as other
prefix unary expressions. A comparison is outside the operand:

```neri
if try query.count() == 1
  console::println("Exactly one row")
end
```

Parenthesize extraction before accessing a member of the successful value:

```neri
let name = (try query.single()).name
```

A failure returns from the nearest enclosing function, method, or closure. It
does not escape from a closure into the function that created it. Lazy Boolean
operators and conditional expressions retain their ordinary evaluation rules.
An optional success payload remains optional and requires narrowing before
member access.

## Library contract

An enum opts in explicitly by naming its success and failure cases:

```neri
@propagation("Accepted", "Rejected")
enum Checked<T, E>
  case Rejected(reason: E)
  case Accepted(value: T)
end
```

The enum has exactly two distinct cases, each with one payload. Case names and
declaration order are chosen by the library. The compiler validates the
annotation and records the resolved cases; it does not recognize application
names or namespaces. An unannotated enum does not acquire propagation semantics
merely because it has two cases.

The enclosing return enum may use another success type or another opted-in enum.
Its failure payload must accept the source failure without an implicit error
conversion. Use an explicit mapping when error types differ.

`result::Result<T, E>`, Neri Data `QueryValue<T>`, and Neri Data `QueryResult<T>`
declare this contract. Resource-bearing propagation payloads are rejected until
their ownership can be transferred through this operation. Existing resource
scopes retain their cleanup rules, including observable close failures through
`resources::Outcome<T, E>`.

## Interactive sessions

A session submission can use `try` without declaring an enclosing function. The
session compiler infers its error type and generates a typed evaluator. All
top-level `try` expressions in that submission must have the same error type;
use `match` or explicit error mapping when combining different errors. Declared
functions and closures retain the ordinary return-type rules.

On failure, the console displays the error payload and accepts another
submission. New bindings and local-variable writeback from the failed submission
are not committed. Mutations to existing objects and external effects, including
database writes, are not rolled back by the session.

## References

The control-flow model is informed by the
[Rust Reference's propagation expression](https://doc.rust-lang.org/reference/expressions/operator-expr.html#the-try-propagation-expression):
success continues with a value and failure returns to the caller. Neri uses an
explicit enum annotation and the prefix keyword `try`.

Daan Leijen's [Koka: Programming with Row-Polymorphic Effect Types](https://www.microsoft.com/en-us/research/publication/koka-programming-with-row-polymorphic-effect-types/)
(MSR-TR-2013-79, updated paper) motivates making possible effects visible in type
signatures. Neri's result propagation is a smaller mechanism: it does not
implement Koka's effect inference, handlers, or formal guarantees.

These primary references were checked on 2026-09-24.
