# Language reference

Neri compiles typed source to native code. Debug and Release preserve the same
arithmetic, null, bounds, evaluation-order, and lifetime rules.

## Find a language rule

| Topic | Section |
| --- | --- |
| Tokens, precedence, evaluation order | [Source and expressions](#source-and-expressions) |
| Numbers, strings, optionals, mutation | [Values and variables](#values-and-variables), [readonly views](#readonly-views) |
| Declarations, visibility, inheritance | [Functions, modules and classes](#functions-modules-and-classes) |
| Type parameters and constraints | [Generics](#generics), [generic contracts](#generic-contracts) |
| Enumerated cases and matching | [Closed alternatives](#closed-alternatives) |
| `fn`, captures, shared callbacks and tasks | [Function values and closures](#function-values-and-closures) |
| C scalar types, records, pointers and borrows | [Unsafe and memory boundary](#unsafe-and-memory-boundary) |
| `@cabiImport`, `@cabiExport`, `cabi fn`, C headers | [C interoperability](C-INTEROP.md) |
| Disposal and resource scopes | [Resources](#resources) |
| Run, check, build and emit | [Tooling](#tooling) |

## Source and expressions

Names are case-sensitive Unicode identifiers. `#` introduces a line comment.
Newlines separate statements, blocks end with `end`, and indentation is cosmetic.
Delimited arguments and array literals may span lines. String literals support
`\"`, `\\`, `\0`, `\n`, `\t`, and `\r` and contain canonical UTF-8.

Precedence, from strongest to weakest:

| Operators | Associativity |
|---|---|
| Calls, member access, indexing | Left |
| Unary `+`, `-`, `!`, `&`, `*` | Nested unary |
| `as` | Left |
| `*`, `/` | Left |
| `+`, `-` | Left |
| `==`, `!=` | Left |
| `<`, `<=`, `>`, `>=` | Left |
| `&&` | Left |
| `||` | Left |
| `condition ? yes : no` | Nested conditional |

Equality binds more tightly than relational comparison. `(1 < 2) == true` is
valid; `1 < 2 == true` and `a < b < c` produce type errors for integer operands.
Expressions, arguments, and array elements evaluate left to right. Logical
operators and conditional expressions evaluate only the required branch.

## Console

Standard-library namespaces are source declarations loaded by `use` or a qualified reference.
`use console` provides terminal input and output: `console::print(value)` writes
without a newline, `console::println(value)` appends a newline, and
`console::read()` reads a line as a string. Both output functions require a
`String`; convert numeric values explicitly with `as String`. End of input
produces an empty string. Output is flushed after each call.

`use test` provides assertions. `test::assert`, `test::assertTrue`, and
`test::assertFalse` require a `Bool`. `test::assertEqual` requires two arguments
of the same type, with equality supported by the language's `==` operator.

## Values and variables


| Type | Contract |
|---|---|
| `Int` | Signed 64-bit integer; checked addition, subtraction, multiplication and negation. |
| `Float` | IEEE-754 binary64, including infinities, NaN and signed zero. |
| `Byte` | Unsigned 8-bit integer. |
| `Bool` | Boolean logic and equality. |
| `String` | Sealed core class with specialized immutable UTF-8 storage, concatenation, and ordinal value equality. |
| `Void` | No returned value. |
| `T[]` | Homogeneous fixed-length array, checked indexing, `Length`, and `for` iteration. |
| `T?` | Explicit optional value. |
| Class | Managed reference to an instance. |

Neri has no built-in `Any` type or dynamic escape hatch. A user-defined class
named `Any` is an ordinary class. Generic type parameters preserve the type
relationships established by their arguments.
Built-in type names and the compiler's `Error` and `Null` type markers are
reserved in type declarations and generic parameter lists.

Integer division truncates toward zero; division by zero and minimum Int divided
by -1 panic. Float arithmetic follows IEEE-754 without fast-math.
Float equality considers two NaNs equal; ordered comparisons with NaN are false.
Numeric casts are explicit: `Int`/`Float`, checked `Int` to `Byte`, and lossless `Byte` to `Int`.
Float-to-Int truncates and panics for unrepresentable values. Numeric `as String`
conversions are locale-independent. Arrays have no equality operator. Classes
can define equality through an annotated instance method.

For built-in numeric scalars, `Bool`, and `String`, equality and inequality
also accept `T` and `T?` in either operand order. The underlying types must
match exactly; this does not introduce numeric conversions. A missing optional
value compares unequal to every required value, and a present optional value
uses the underlying value comparison. Both operators return `Bool`. This rule
does not extend ordered comparisons or lift user-defined class operators.

`String` is loaded from the core library without a `use` directive. It is a
source-declared, sealed class whose storage remains the runtime UTF-8 string;
it is not a wrapper around another text value. String literals and
`String.fromBytes(bytes)` create values. `fromBytes` returns `null` for invalid
UTF-8. `new String()` and inheritance from `String` are unavailable.

Readonly instance methods expose byte length, nullable byte access, checked
byte slices, scalar boundaries, scalar count, scalar access, concatenation, and
equality. A scalar is a Unicode scalar value rather than a grapheme cluster.
The `text` library retains matching namespace functions and `text::Scalar` for
explicit scalar construction and UTF-8 encoding.

`sealed` closes an ordinary class to inheritance. `@representation("utf8")`
selects the registered storage for the canonical public `String` declaration in
`@stdlib/core.hk`. That declaration has methods and no fields, base, type
parameters or constructor. Literal and factory construction maintain its storage
invariants. `@intrinsic` module functions have registered exact signatures and
empty bodies; the compiler supplies their runtime calls. An intrinsic identifier
selects a closed compiler registry entry containing its native symbol, effects,
runtime version and feature requirements. A source declaration cannot introduce
an arbitrary native symbol through `@intrinsic`.

`@exact` marks a generic or non-generic module function whose arguments must
have exactly the instantiated parameter types. Ordinary functions continue to
accept assignable subtype arguments. The standard-library `test::assertEqual<T>`
uses this rule, evaluates its arguments once from left to right, and applies the
ordinary `==` operation for `T`.

This split follows the established compiler-library boundary in Rust: language
items let source libraries provide compiler-known operations, while compiler
intrinsics are registered implementation details normally exposed through
library wrappers ([Rust compiler language items](https://rustc-dev-guide.rust-lang.org/lang-items.html),
[Rust core intrinsics](https://doc.rust-lang.org/core/intrinsics/)).

The dedicated immutable representation follows the public string contracts in
[.NET String.cs](https://github.com/dotnet/runtime/blob/main/src/libraries/System.Private.CoreLib/src/System/String.cs)
and [OpenJDK 25 String.java](https://github.com/openjdk/jdk/blob/jdk-25-ga/src/java.base/share/classes/java/lang/String.java).
Keeping the representation behind a source-level class applies the abstraction
boundary described by [Liskov and Zilles, Programming with Abstract Data Types (1974)](https://gleitzman.com/media/docs/adt-liskov.pdf).

`let` and parameters are immutable bindings; `var` permits reassignment. Binding
immutability does not freeze the fields of an object. An explicit annotation
supplies the element type for `let values: Int[] = []`. `let values = []` fails
because there is no element type to infer.

Only `T?` accepts `null`. Proven checks such as `x != null` refine optional locals
on the corresponding control-flow path. Access requires that refinement.
`T?[]` and `T[]?` differ. `Void?` and repeated optional suffixes are invalid.

Null guards also refine field paths rooted in a local or parameter. For example,
`row.right != null ? row.right.total : null` accesses `total` only in the branch
where `right` is present. Each optional intermediate field needs its own guard.
Field-path facts are conservative: calls and mutations can invalidate them,
including writes through another alias. Saving a field in an immutable local
provides a stable value when a longer-lived refinement is needed.
Loop bodies establish their field guards again for each iteration; inherited
member-path facts are cleared at loop boundaries. Alternative branches are
analyzed independently and retain only common incoming facts at their merge.
An initializer uses its refined expression type for inference. A mutable cursor
that must later accept null therefore needs an explicit optional annotation,
such as `var cursor: Node? = container.first` inside a guard on `container.first`.

A conditional expression preserves the type of matching branches. It joins
`T` with `null` or `T` with `T?` into `T?` in either order. The underlying
`T` must match exactly; conditional expressions do not introduce numeric or
class conversions. Only the selected branch evaluates. A null check in the
condition narrows an optional name or guarded field path in the branch where it is present; that
refinement does not extend past the conditional expression. This is Neri's
own rule; C#'s
[conditional operator](https://learn.microsoft.com/en-us/dotnet/csharp/language-reference/operators/conditional-operator)
provides a related comparison, including distinct target-typed conversions.

The field-path rules draw on flow-sensitive reasoning described in
[Logical Types for Untyped Languages](https://www2.ccs.neu.edu/racket/pubs/icfp10-thf.pdf)
(Tobin-Hochstadt and Felleisen, ICFP 2010), particularly refinement from positive
and negative predicate outcomes and reasoning about data structure components.
Neri implements its own bounded rules and mutation invalidation; the paper's
formal results are not a proof of this implementation. The
[C# nullable analysis specification](https://github.com/dotnet/csharplang/blob/main/proposals/csharp-9.0/nullable-reference-types-specification.md)
is a related reference for tracked field expressions. These sources were checked
on 2026-09-23.

### Readonly views

`readonly T` provides a transitive read-only view of a managed object or array.
The view refers to the original storage: creating it does not copy or freeze the
object. Other mutable aliases still observe and can modify that storage.

```neri
class Counter
  public value: Int = 0

  public readonly def read(): Int
    return this.value
  end
end

def total(values: readonly Counter[]): Int
  var result = 0
  for value in values
    result = result + value.read()
  end
  return result
end
```

Fields, array elements, iteration values and narrowed optionals preserve the
view on references reached through them. Scalar values and immutable strings
retain their ordinary types. Mutable references may be passed to readonly
parameters; readonly references cannot be passed or returned as mutable types,
cast to mutable types, or exposed as writable borrowed storage. Readonly arrays
retain their element-type identity rather than providing array covariance.

`readonly T[]` restricts both the array and references reached through its
elements. `(readonly T)[]` is a mutable array of readonly references: slots can
be replaced, while objects accessed through those slots remain readonly.

`readonly` applies to an instance method and makes its `this` reference readonly.
Overrides preserve that receiver contract. Such a method may return a fresh
mutable object, but an object reached through `this` requires a readonly return
type. Calls through readonly receivers require readonly methods; ordinary
callbacks do not carry that receiver contract. A readonly method is not a pure
function: its other parameters and external effects keep their declared contracts.

Views participate in generic specialization and inference. They are checked by
the frontend and use the ordinary runtime representation. Native pointer fields
and writable native borrows are unavailable through a readonly view. A readonly
view alone is not proof of exclusive access or safe cross-thread sharing.

### Reference identity

`reference::same(left, right)` reports whether two managed class references
refer to the same object. Import it with `use reference`. Equal field values in
different objects do not make their references identical. Readonly class views
preserve object identity. This operation accepts a common class type; scalars,
represented native types, function values, arrays, and optional references are
outside this contract. It does not invoke a user-defined equality operator or
expose an address.

## Functions, modules and classes

Functions declare parameter and return types. Trailing default arguments,
recursion and forward calls are supported. An executable unit defines exactly
one non-namespaced top-level `main(): Void` with no parameters. Library units
must not declare `main`. Non-Void functions
return on every statically recognized path. Overloading is outside this surface.

Calls to declared functions, methods and constructors accept named arguments:

```neri
def rangeLength(start: Int, finish: Int = start + 1): Int
  return finish - start
end

def main(): Void
  let length = rangeLength(finish: 10, start: 3)
end
```

Each label binds to a parameter in the resolved declaration. Positional arguments
precede named arguments. Named arguments may appear in any order; each parameter
is supplied at most once. Required parameters must be supplied, and omitted
defaulted parameters use their declared expressions. Labels are case-sensitive.
Renaming a public parameter changes the named-call source API.

The receiver of an instance call is evaluated first. Supplied argument expressions
are evaluated exactly once in source order, followed by omitted defaults in
declaration order. Defaults can use earlier parameter values. Parameter placement
does not reorder evaluation. Virtual calls use the compile-time receiver's
declaration for labels and defaults and preserve runtime method dispatch.

Generic inference matches each argument to its labeled parameter before using
its type. Contextual expressions, including empty arrays, receive the resolved
parameter type through the ordinary inference and checking rules. For example,
`headOr(values: [], fallback: 7)` can infer `T = Int` when `headOr<T>` declares
`values: T[]` and `fallback: T`.

Function values retain their positional function-type contract; parameter labels
are available on declared callables. Alternative-case payload construction and
`native::*` intrinsic syntax use positional arguments. Declared `@intrinsic` and
`@cabiImport` functions expose the labels in their Neri signatures. Arrays contain
expressions rather than labeled arguments. Named calls to unsafe and C ABI
declarations require every parameter explicitly (`NR275` for omitted defaults).

`if`, `while`, and `for` have lexical scopes. A `for` element binding is immutable.
`break` and `continue` require an enclosing loop.

A single-line assignment, call, `return`, `break`, or `continue` may use a
postfix condition: `result = result + value if value != null`. The condition
is checked before the action and refines optional types inside that action.
Postfix conditions have no `else`, `end`, or chained modifier. Declarations and
multiline actions use a block instead. Block `if` bodies begin on the next line;
`else` and `end` begin separate lines. `else if` chains remain supported.

All supplied source files contribute to one compilation module. `namespace`
applies to subsequent declarations. `::` qualifies namespaces; `.` accesses type
and instance members. Qualified paths resolve from the root namespace.

```neri
namespace App::Services

class Printer
  static def print(): Void
    console::println("ready")
  end
end
```

`App::Services::Printer.print()` selects the static method without an import.
`use App::Services` opens that namespace throughout the module.
`use Services = App::Services` defines a module-wide namespace alias:
`Services::Printer.print()`. An alias target is a root namespace; alias targets
do not resolve through other aliases. Alias names must be unique and distinct
from root namespace names. Local value names do not shadow namespace qualifiers.

A `use` or qualified reference to a bundled standard-library module loads its
sources and transitive dependencies. Project namespaces resolve within supplied
sources and explicit project references. Duplicate or ambiguous declarations are
errors.
An unqualified function name resolves in the current namespace before imported
namespaces, including when an imported function is generic. Explicit type
arguments apply to the selected declaration; a selected ordinary function reports
`NR220` when given type arguments. Qualified names select their stated namespace.
See [binary file reads](FILES.md) for the bounded file API.

Module scope contains only namespace/use directives and function/class declarations;
other tokens produce a parse diagnostic.

Declaration modifiers are keywords before `class` or `def`. When combined, they
use this order: access (`public`, `internal`, `protected`, or `private`),
`abstract` or `sealed`, `override`, `static`, `readonly`, `unsafe`, `resource`,
then `class` or `def`. Only modifiers supported by that declaration kind may
appear. Compiler annotations such as `@cabiImport`, `@cabiExport`, `@intrinsic`,
`@operator`, `@conversion`, `@exact`, `@operation`, `@propagation`, and `@representation` remain
annotations.

`try expression` extracts an opted-in result's success payload or returns a
compatible failure from the enclosing callable. `match` remains available for
handling both cases locally. Result enums declare their cases with
`@propagation("SuccessCase", "FailureCase")`; see [typed propagation](TRY.md)
for precedence, error compatibility, and resource cleanup rules.

Classes have single inheritance. Classes default to `internal`, fields to
`private`, and methods to `public`. `internal` is module visibility, `private`
is declaring-class visibility, and `protected` includes derived classes.
Instance methods dispatch virtually; exact name and signature override a base
method. `override` asserts that relationship. `static def` declares a method
without a receiver. `super.method()` dispatches directly to the base method.
Omitted arguments use defaults from the statically resolved declaration; supplying
those defaults preserves virtual dispatch to the receiver's implementation.

`abstract` marks an ordinary class as incomplete, so the class cannot be
constructed directly. An abstract class may declare abstract instance methods.
Each abstract method is a signature without a body or its own `end`; it is safe
and cannot be `init`, static, or private. Abstract classes cannot also be sealed,
resources, represented classes, or enums.

A concrete subclass implements every inherited method whose nearest declaration
is abstract. An implementation follows the ordinary override rules: its name,
parameter and result types, visibility, and readonly receiver contract match the
abstract declaration exactly. An abstract subclass may leave an obligation
unimplemented or replace it with another abstract declaration.
`super.method()` requires a concrete implementation in the resolved base method.

```neri
abstract class Shape
  abstract def area(): Int
end

class Square: Shape
  override def area(): Int
    return 4
  end
end
```

This completeness model follows the established rules for abstract
[types and methods in Crystal](https://crystal-lang.org/reference/1.20/syntax_and_semantics/virtual_and_abstract_types.html),
[classes](https://learn.microsoft.com/en-us/dotnet/csharp/language-reference/language-specification/classes#15222-abstract-classes)
and [methods](https://learn.microsoft.com/en-us/dotnet/csharp/language-reference/language-specification/classes#1567-abstract-methods)
in the C# language specification and the Java Language Specification rules for
abstract [classes](https://docs.oracle.com/javase/specs/jls/se8/html/jls-8.html#jls-8.1.1.1)
and [methods](https://docs.oracle.com/javase/specs/jls/se8/html/jls-8.html#jls-8.4.3.1).

Construction initializes base classes before derived classes. `init` is not
inherited. An explicit `super(args)` starts a derived initializer when the base
requires arguments. A valid zero-argument base call is implicit. Inherited fields
cannot be redeclared.
An explicit `super(args)` occurs once, as the first statement of a derived
`init`; it is invalid inside another control-flow body or an ordinary method.

A class without `init` has an implicit zero-argument constructor. Construction
initializes its ancestors first, invoking the nearest declared initializer with
its default arguments, then initializes the remaining fields in inheritance
order. That initializer must be accessible from the derived class and accept
zero supplied arguments. Intermediate classes cannot skip initialization or
constructor visibility.

### Library operators and explicit conversions

Public, safe instance methods on ordinary classes can expose operators using
`@operator("+")`. The annotation belongs to the class that implements the
operation. Binary operators take one parameter of exactly that class type;
unary operators take none. Arithmetic operators return that class type;
comparison operators and unary `!` return `Bool`.

Supported binary tokens are `+`, `-`, `*`, `/`, `==`, `!=`, `<`, `<=`, `>`, and
`>=`; unary tokens are `+`, `-`, and `!`. Each token and arity has one declaration
per class. `==` and `!=` are independent operations. Logical `&&` and `||` retain
their language-defined short-circuit behavior.

`@conversion` marks a public, safe instance method with no parameters and a
non-`Void` return type. The return annotation supplies the target of `value as T`.
There is one conversion per target type in a class. Identity conversions and
conversions that remove a readonly view retain the ordinary type rules.
Assignments, arguments and returns require their declared types; the compiler
does not insert calls to conversion methods.

These annotations use ordinary instance dispatch, source methods and generic
specialization. Operands are evaluated once, in order. Readonly receivers require
`readonly` methods. Resource classes use their ownership operations and cannot
declare these annotations. Operator and conversion methods remain callable by
name. Adding a library type requires no new compiler case for its name.

See [text](TEXT.md) for `text::Scalar`, a library class with validated construction,
explicit conversion to `Int` and Unicode encoding implemented in Neri.

## Generics

Module functions and classes may declare type parameters:

```ruby
def identity<T>(value: T): T
  return value
end

class Box<T>
  public value: T

  def init(value: T): Void
    this.value = value
  end
end
```

`identity(42)` infers `T` as `Int`; `identity<Int>(42)` supplies it explicitly.
The opening `<` in an explicit function specialization is adjacent to its name.
Class construction supplies its arguments: `new Box<String>("Neri")`.
Arguments may themselves be arrays, optionals, callbacks, or generic classes.
`Box<Int>` and `Box<String>` are distinct, invariant types. An assignment through
`Box<T>.value` must satisfy the concrete `T`, including after passing the box
through a function or a callback.

Inference uses the expected result type and argument types. For example,
`let empty: String? = absent()` can infer `T` for `absent<T>(): T?`, and
`let copy: fn(Int): Int = identity` specializes the function value. An expected
type supplies callback annotations once the generic parameters in that signature
are known. A callback whose result type remains unknown requires an explicit
return annotation or an expected result type at the call site. `null` and an
untyped empty array alone do not determine a type parameter. Conflicting arguments
produce a type error; inference introduces no numeric or unchecked conversions.

Signatures name every parameter and return type. Each used specialization is
type-checked with concrete arguments, including its body and initializers, and
compiled once per compilation module. Unconstrained generic bodies are checked when
specialized. Type parameters accept safe value types; `Void`, raw pointers and
unresolved types cannot be type arguments. The entry point and C ABI imports have
concrete signatures.

### Generic contracts

A source-declared contract names the operations a type parameter may use:

```neri
contract Named
  readonly def label(): String
  end
end

def read<T: Named>(value: T): String
  return value.label()
end
```

Each requirement is a unique public, safe instance signature with no body,
defaults, abstract or static modifier, or type parameters. A requirement can use
`Self`, `Self?`, or `Self[]`; a nested generic use such as `Box<Self>` is not supported.
A contract itself has no type parameters. A generic module function may put one
source-declared contract after each type parameter's colon. Class bounds and
generic compiler operations do not accept contracts.

Requirements match structurally. An implementing class supplies an accessible
public instance method with the exact parameter and result types after replacing
`Self`; matching does not insert conversions. A `readonly` requirement requires
a readonly implementation. Contract requirements have no runtime value and do
not introduce dynamic dispatch. Rename does not support contract requirements,
because structural implementations do not declare their relation to a contract.

`@operator` labels a requirement for an operator expression. For example,
`@operator("==") def equals(other: Self): Bool` permits `left == right` for a
constrained `T`; it does not add an `equals` method alias to `T`. Supported labels
and their return and operand shapes are the ordinary operator rules described
above.

The core library declares `Equality` as an ordinary contract with a `==`
requirement. Its spelling has no separate generic rule. Built-in operator
implementations and source classes are checked against the same requirement.
`test::assertEqual<T: Equality>` therefore uses the declared contract. A contract
states callable shape only; it does not promise algebraic laws such as
reflexivity, symmetry, or transitivity.

Constrained templates are checked at their declaration with their type parameters
opaque. Unconstrained generic functions retain specialization-time body checking.
This is Neri's current design, not a claim of formal proof or a full trait system.
It follows the general idea of stated generic requirements in
[Siek and Lumsdaine](https://arxiv.org/pdf/0708.2255),
[Go interface method sets](https://go.dev/ref/spec#Interface_types), and
[Rust trait bounds](https://doc.rust-lang.org/reference/trait-bounds.html).

The current generic surface consists of module functions, classes, and
method-level type parameters. A method can introduce its own type parameters
on an ordinary or specialized generic class; for example,
`receiver.identity<String>("value")` or an inferred `receiver.identity("value")`.
See [generic methods](GENERIC-METHODS.md) for inference, scope, and restrictions.
Static methods accept an explicitly specialized class
receiver, such as `Container<Int>.create(42)` or
`library::Container<String>.create("value")`; constructor and method visibility
still apply. Generic class inheritance,
class bounds, multiple bounds, generic contracts, and higher-kinded types are
outside this surface. Templates are supplied as source files in the same
compilation invocation, including across namespaces. A compiled specialization is concrete; it is not a separately
importable generic template or a package ABI promise.

Compilation permits 4096 distinct generic specializations in total and 128
specializations of each generic template. It also permits 32 nested class
instantiations, 64 levels of written type nesting, and canonical type arguments
of at most 1024 UTF-8 bytes. Ordinary recursion reuses the same specialization;
recursion that creates an unbounded sequence of new types reports `NR222`.
`NR220` reports invalid generic declarations or type-argument counts; `NR221`
reports arguments that cannot be inferred or used as safe value types.

## Closed alternatives

An `enum` declares a closed set of one or more named alternatives. Each `case` has zero or
more typed payloads. Payloads do not have default values. A generic enum specializes its payload types with the same
rules as a generic class.

```neri
enum Result<T>
  case Ok(value: T)
  case Error(message: String)
end

def describe(result: Result<Int>): String
  match result
    case Result.Ok(value)
      return value as String
    case Result.Error(message)
      return message
  end
end
```

Construct a value only through an explicit case constructor, such as
`Result<Int>.Ok(42)`. `new Result<Int>()`, inheritance, casts to a different
alternative type, and access to the compiler-generated tag and payload storage
are unavailable. Each match case names the enum that owns it and binds exactly
the payloads declared by that case. Bindings exist only inside their case body.

`match` is a reserved statement keyword. Its scrutinee evaluates once, every declared case must
appear exactly once, and every case body participates in ordinary return and
control-flow analysis. Missing, repeated, foreign, and wrong-arity cases are
diagnostics. The current pattern surface is deliberately flat: cases select one
closed constructor and bind its whole payload, with no wildcards, nested
patterns, ranges, or or-patterns. Managed payloads, including generic objects
captured by closures, retain the normal managed-field lifetime and survive
collection.

## Function values and closures

`fn(Int): String` is a function type. Function types are invariant: parameter
and return types must match exactly. Values can be passed, returned, stored in
fields and arrays, and invoked with ordinary call syntax. Named safe top-level
functions are values; their default arguments apply to direct calls, while calls
through a function value supply the full signature.

```text
def apply(value: Int, operation: fn(Int): Int): Int
  return operation(value)
end

def offsetBy(offset: Int): fn(Int): Int
  return fn(value)
    return value + offset
  end
end
```

Use a trailing `do` block to pass an inline callback to a call:

```text
let answer = apply(40) do |value|
  return value + 2
end
```

The block is appended after the explicitly supplied positional arguments. It
binds to the immediately preceding call (including method, function-value and
constructor calls), and uses that parameter's expected function type. Any later
parameters must have defaults. It does not search the signature for a callback
slot or reorder arguments. With no block parameters, write `call() do` followed
by the body on a new line. Multiple parameters use `do |left, right|`; explicit
annotations use `do |value: Int|: Int`.

`do` must follow the closing parenthesis on the same line. The body starts on
a new line and its closing `end` occupies its own line. A trailing block cannot
appear inside another call's argument list or be followed by chaining or `end)`.
Assign its call result to a local binding before passing that result elsewhere.
Nested calls with their own `do` blocks inside the body are supported. `return`
returns from the callback, not from the function enclosing the call.

`fn(parameters) ... end` creates a standalone closure for a binding, field,
return or array element. Inline `fn` closures inside call arguments are rejected
with `NR009`; use `do` or pass a named callback instead. `NR009` also rejects a
block attached to a non-call or an invalid block ending. The function type syntax
remains `fn(Int): Int`.

An expected function type supplies
omitted parameter and return annotations in trailing blocks, returns, annotated
initializers, assignments and typed array elements. Explicit annotations must
match that contract. Without an expected type, annotate every parameter and the
return type, such as `fn(value: Int): Int ... end`. Every non-Void callback must
return on all statically recognized paths, just like a named function.

Parentheses apply a suffix to the whole function type: `(fn(Int): Int)?` is an
optional callback, `(fn(Int): Int)[]` is an array of callbacks, and `fn(Int): Int?`
is a callback returning an optional integer. Optional callbacks require a null
check before invocation.

Closures capture referenced `let` bindings, parameters and lexical `this` by
value. Captured managed objects, strings, arrays and other closures remain alive
as long as the closure needs them, including after the enclosing function returns.
Captured object references retain their ordinary field mutability and lexical
member access. Optional captured bindings can be refined by null checks.

Capturing a `var` binding or raw pointer is a compile error. A local immutable
snapshot can be captured instead of a `var`; a raw pointer is never a managed
capture. Callback signatures contain safe value types, and an unsafe enclosing
block does not grant unsafe access inside a callback. Unsafe and C ABI functions
require an explicitly written callback wrapper with its own unsafe block.

### Typed quotations

`quote fn(Parameters): Result` describes an inspectable expression. An explicit
`quote do |parameter: Type|: Result ... end` expression constructs one. A
quotation parameter also supplies the expected types for a trailing `do` block.
The compiler preserves resolved field identities, parameter types and scalar
captures in a readonly tree exposed through `tree()`.

Quotations use `use quotation` and a body containing one supported return
expression. Their signatures participate in ordinary generic inference. See
[typed quotations](QUOTATIONS.md) for supported operations, capture rules and
diagnostics.

### Typed field arguments

`fields of T` preserves a declared class's identity in a collection of supplied
field values. A final `labels values: fields of T` parameter accepts named
arguments checked against `T`'s public instance fields. Generic inference,
completion and navigation use the entity declarations. See
[typed field arguments](FIELDS.md) for presence, evaluation and inspection rules.

### Shared callbacks

`shared fn(Parameters): Result` gives a callback read-only access through its
captures. Managed captures are transitive views of the same objects, not copies.
Captured `this`, arrays and nested closures obey the same rule. A captured
callback can be invoked only when it also has a shared contract. Local variables,
newly created objects and explicitly mutable arguments retain their ordinary
mutability.

```neri
def apply(value: Int, callback: shared fn(Int): Int): Int
  return callback(value)
end

def main(): Void
  let offset = 3
  let result = apply(7) do |value|
    return value + offset
  end
end
```

The expected type supplies the shared contract to a `do` block, closure or named
function value. A shared callback can be assigned to an ordinary function type
with the same parameters and result. An ordinary callback value cannot be
upgraded by assignment or cast. A readonly view of a shared callback remains
invocable; a readonly view of an ordinary callback does not establish a shared
contract.

This is a capture-access contract, not a purity or synchronization guarantee.
Sequential aliases may still modify the captured objects, and callbacks may
perform I/O. Shared callbacks alone do not freeze an object graph, establish a
task lifetime or authorize concurrent native-service access.

### Parallel callback contracts

`parallel fn(Parameters): Result` combines shared capture access with checked
call effects. Managed allocation, local mutation, checked arithmetic and calls
to other verified functions are permitted. I/O, host services, C ABI calls,
native allocation and `unsafe` operations are rejected, including through
transitive calls. Callback invocations require a `parallel fn` type. Verified
`math` declarations and `test` assertions satisfy the runtime-call contract.

```neri
def apply(value: Int, callback: parallel fn(Int): Int): Int
  return callback(value)
end
```

The expected type supplies the contract to closures, trailing `do` blocks and
named function values. A parallel callback can widen to `shared fn` or `fn` with
the same signature. Assignment and casts preserve these capability boundaries;
shared and ordinary callback values cannot be upgraded to `parallel fn`.

Direct calls use the ordinary callback calling convention. The type checks
captures and effects; a direct call does not schedule a task or establish
exclusive ownership of explicit mutable arguments.

### Sequential array generation

`use arrays` provides `arrays::generate(count, callback)`. Its ordinary
`fn(Int): R` callback runs on the calling thread once per index, in increasing
order, and returns an `R[]`. It can capture mutable local state and return
existing mutable object references; array elements retain those identities.
The expected array type, an explicit `arrays::generate<R>` argument, or a typed
callback supplies `R`. Elements follow the same safe array storage contract as
array literals. A zero count returns an empty array without calling the
callback; a negative count panics.

```neri
use arrays

def numbers(count: Int): Int[]
  return arrays::generate(count) do |index|
    return index * 2
  end
end
```

### Scoped task generation

`use tasks` provides `tasks::generate(count, callback)` and
`tasks::generate(count, parallelism, callback)`. The callback has type
`parallel fn(Int): R`; the result is a new `R[]` in index order.
The generic source declaration carries `@operation("tasks.generate")`; this
closed operation identifier selects task-generation binding while source
navigation and completion use the declaration's namespace and function name.
Its required shape is `(Int, Int = 0, parallel fn(Int): R): R[]` with an empty,
safe body.

```neri
use tasks

def squares(count: Int): Int[]
  return tasks::generate(count) do |index|
    return index * index
  end
end
```

The expected array type, an explicit `tasks::generate<R>` argument, or a typed
callback supplies `R`. Elements use the array storage contract: scalars, managed
references, or nullable managed references. An empty range returns an empty array
without invoking the callback. Count must be nonnegative. Parallelism is an Int
between zero and 4294967295; zero selects the available hardware parallelism.
Invalid runtime values panic.

The caller waits for every task. Captures are shared as read-only views without
copying their object graphs. Each callback may allocate and mutate local objects;
returned objects remain valid and retain their aliasing after the call. Each
result slot is exclusive to its index and is invisible to other callbacks.
Execution order is unspecified; callbacks cannot rely on sibling ordering.
Nested generation reuses the outer worker pool and makes progress with one
participant. Each outer call includes worker startup and joining.

### Isolated persistent workers

`use workers` provides `workers::start(entry, config, options)`, returning a
`Result<Pool, Failure>`. The named entry has the exact signature
`fn(Byte[]): Void`. Each worker owns its heap and creates its application state
from a copied configuration. Requests and responses cross the boundary as copied
byte arrays. Closures, function variables, instance methods and C ABI entrypoints
are rejected. Workers support I/O on exclusively owned resources; they do not
share the parent's managed state.

Acquire the pool with `using` and finish each worker's resources before its entry
returns. Admission is bounded by both outstanding requests and reserved payload
bytes. Cancellation is cooperative. See [Isolated workers](WORKERS.md) for the
typed API, ownership contract, limits and executable examples.

The source operation declaration names its bridge explicitly with
`@operation("workers.start", bridge)`. Its safe empty body declares a first
parameter `fn(Byte[]): Void`, a second parameter `Byte[]`, and any remaining
ordinary parameters without defaults. The unsafe module bridge replaces only
the first parameter with `cabi fn(Byte*, UInt64): Void`; remaining parameter types
and the return type must match exactly. The operation is called directly and
cannot itself be converted into a function value.

## Unsafe and memory boundary

### Explicit-width numbers

`Int32`, `UInt32`, `UInt64`, and `Float32` preserve their widths in storage and
C ABI calls. They are distinct types; assignments and arithmetic do not implicitly
mix widths or signedness. Integer literals have type `Int`, floating literals
have type `Float`; use an explicit numeric conversion at a native boundary.

| Type | Representation | Range |
| --- | --- | --- |
| `Int32` | Signed 32-bit integer | −2147483648 to 2147483647 |
| `UInt32` | Unsigned 32-bit integer | 0 to 4294967295 |
| `UInt64` | Unsigned 64-bit integer | 0 to 18446744073709551615 |
| `Float32` | IEEE 754 binary32 | 24 significant binary digits |

```ruby
let width = 800 as Int32
let mask = 4294967295 as UInt32
let scale = 1.5 as Float32
let ordinaryWidth = width as Int
```

Numeric `as` conversions involving these types check integer range and signedness.
Float-to-integer conversion rejects NaN, infinity and values outside the target
range, then truncates toward zero. Float narrowing rejects finite values outside
the finite target range; NaN, infinity and signed zero are preserved. Conversions
to floating point round to the target precision; underflow may round to zero.
Arithmetic requires matching operand types. Integer addition, subtraction,
multiplication, negation and division check overflow and division by zero,
including unsigned underflow. Failures panic with `NRP002` and exit status 70.

The types work in fields, arrays, optionals, generic values, native allocations
and pointers. Their raw memory operations follow the unsafe rules below.
String formatting accepts `Int`, `Byte` and `Float`; explicitly convert a new
numeric type to an appropriate formatting type when its range permits it.

### Native layout declarations

`struct` declares fields in source order with natural C alignment and tail
padding. `union` places every field at offset zero and uses the largest member
size, rounded to the largest member alignment. These declarations are distinct
from managed classes. Fields may contain numeric types, Bool, native pointers,
other native layouts, and fixed arrays written `T[count]`. Counts are positive
integer literals. Managed references and recursive inline layouts are rejected;
recursion through pointers is valid.

```ruby
struct Point
  x: Float32
  y: Float32
end

union EventStorage
  point: Point
  bytes: Byte[128]
end

def main(): Void
  let pointSize = native::sizeOf<Point>()
  let alignment = native::alignOf<EventStorage>()
  let yOffset = native::offsetOf<Point>("y")
end
```

`sizeOf` and `alignOf` take one type argument and no value arguments;
`offsetOf` additionally takes a literal field name. Results are byte counts of
type `Int`, and the queries are safe operations. Layouts target the supported
64-bit native platforms, are limited to one GiB and 128 nested types, and use
four-byte alignment for 32-bit numbers and eight-byte alignment for 64-bit
numbers and pointers.

Native records are value types: copying a record copies its inline fields.
`stackalloc Record[count]` and `native::allocZeroed<Record>(count)` provide
contiguous record storage with the declared alignment. Pointer arithmetic uses
the complete record size, including tail padding. Inside an unsafe block,
`pointer.field` reads or writes a field and `&pointer.field` obtains its typed
address. The same syntax accesses nested records and union members. Fixed
arrays support checked indexing, including when embedded in another record.
Field assignment on a local record requires a mutable `var` binding.

```ruby
struct Sample
  position: Float32[2]
  code: UInt32
end

def main(): Void
  unsafe
    let sample = native::allocZeroed<Sample>(1)
    sample.position[0] = 1.5 as Float32
    sample.code = 42 as UInt32
    let codeAddress = &sample.code
    var copy = *sample
    copy.code = 7 as UInt32
    native::free(sample)
  end
end
```

Union members share storage and have no automatic active-member tag. Native
code must establish which member is valid before reading it. C ABI declarations
exchange records through typed pointers.

### Raw memory operations

Raw pointer operations require `unsafe def` or an `unsafe ... end` block. `T*`
is non-null; `T*?` requires a null check before access. Raw pointers do not retain
managed allocations. Address-of applies to mutable unmanaged locals.

`stackalloc T[count]` allocates uninitialized storage for the function lifetime.
`native::alloc<T>` and `native::allocZeroed<T>` allocate manually owned storage.
`native::realloc` consumes the previous allocation and preserves its common prefix;
`native::free` releases native storage. Stack and borrowed storage cannot be freed
or reallocated. Invalid pointer access inside unsafe code may have undefined
behavior; safe wrappers must restore the language invariants before returning.

`borrow values as pointer ... end` exposes an unmanaged array payload for a
lexical scope. The owner remains alive and its address stable. The pointer cannot
escape the scope or acquire ownership. `@cabiImport("symbol")` on an empty unsafe
function declaration imports a C symbol with supported scalar, pointer, and
native function-pointer types.

`@cabiExport("symbol")` on a module-level `unsafe def` exposes its implementation
through a C entry point. `cabi fn(Parameters): Result` is a typed, unmanaged C
function pointer. Imported and exported functions supply these pointers in a
typed context; invoking them requires `unsafe`. Parentheses make the whole
pointer nullable: `(cabi fn(Int32): Int32)?`. Native function signatures accept
C-compatible scalars, pointers, and native function pointers. Managed closures
retain their separate `fn` contract. See [C interoperability](C-INTEROP.md) for
library builds, generated headers, runtime entry, and callback lifetime.

`@library("name")` on a C ABI declaration adds its external library when linking
an executable that imports the function. The name is passed as one `-lname`
argument, with duplicates removed. Names contain ASCII letters, digits, `_`,
`-`, or `.`, start with a letter or digit, and have at most 128 bytes. Library
names are not paths or linker options. Install the library separately; the
compiler does not download dependencies. `NERI_LIBRARY_PATH` adds one absolute
library search directory and embeds it as a runtime search path; macOS ARM64
defaults to `/opt/homebrew/lib`. The platform
linker's standard directories remain available. Shared libraries must also be
available to the operating system's loader when running the program.

```ruby
@library("m")
@cabiImport("cos")
unsafe def cosine(value: Float): Float
end
```

Neri IR preserves library declarations through the `native-libraries-v1`
feature in transport 1.2. Modules without this feature retain transport 1.1.

The runtime [ABI and collection contract](ABI.md) defines roots and allocation
boundaries. Bounds and arithmetic failures panic with exit status 70. Compile
errors produce diagnostics and prevent artifact emission.

## Resources

A `resource class` owns a value that is acquired with `using` and released when
its scope ends. A resource defines `close(): result::Failure?`. Nested resources
close in reverse acquisition order, including after `return`, `break`, or
`continue`.

`transfer name` moves a local resource when it is the returned value of a
callable with the same resource return type. Resource values cannot be copied,
stored, captured, passed to ordinary calls, or cast.

A callable containing `using` returns `resources::Outcome<T, E>`. Its
`completion` contains the value or body failure, and `closeFailures` contains
each cleanup failure in close order. A `using` acquisition may return a resource
or `result::Result<Resource, E>`; the result error type matches the enclosing
outcome error type.

Factories may return `Result<R, E>.Ok(transfer owned)` from a fresh local `R`.
The move invalidates the local binding. Static factories have no owned receiver.
When `T` and `E` are the same type, return an explicit `Outcome` to distinguish
a value from a failure. The compiler requires every cleanup failure to remain
observable in the returned outcome.

Resource implementations make `close` idempotent and reject operations after
closure. The standard-library scoped handles report `closed` or `disposed` for
such operations. Recoverable returns and cooperative cancellation run cleanup;
fatal panic and forced process termination reclaim descriptors through the OS
without executing language cleanup.

## Tooling

`check` validates and emits readable NIR text by default. `--emit=neri-ir-hex`
emits the hexadecimal encoding of the serialized NIR envelope consumed by the
native backend; `--emit=neri-ir` selects the readable inspection format.
A source file without a
subcommand is equivalent to `run`. `build` emits an executable by default, named
after the first source file without `.hk` in the working directory. `--output`
overrides that path. Native emission defaults to the installed runtime manifest's
target; `--target` explicitly selects a target. `--version` prints the compiler
version. `run` validates the sources and executes a native program. On macOS ARM64,
eligible runs reuse a private persistent executable cache; other runs compile in
a unique temporary directory and remove it on normal completion or a reported
child failure. `--no-cache` selects this temporary path. Arguments after `--` reach the
program unchanged. Program exit codes 0..125 propagate; larger statuses map to
125. `run` owns its output location and accepts only executable emission.

The run cache defaults to `$HOME/.neri-run-cache`. `NERI_CACHE_DIR` selects an
absolute directory whose parent exists. The directory must belong to the current
user with mode 0700; an unavailable or unsuitable cache falls back to compilation.
Cache entries are disposable and can be removed when no compiler is using them.
Programs using `@library` or custom linker search/injection environments use the
uncached path. `build` and `check` do not reuse run artifacts.

`--timings` writes phase durations in milliseconds to stderr. Native executable
builds report frontend, codegen and link time; runs also report cache lookup,
total time until ready, and execution. The ready measurement includes preceding
phases and is not an additional phase. Launcher overhead is outside these timings.
See [performance checks](PERFORMANCE.md#run-latency) for cache inputs and measurement.

The `host` library supplies checked UTF-8 byte access, parsing, atomic file writes,
path operations, environment and argument access, and shell-free process spawning.
Fallible operations return an optional or Bool and expose `errorMessage()`.
`appendByte`, `appendInt`, and `appendString` allocate a new array and copy the
input; they preserve the original array. Repeated append therefore has quadratic
copy cost. Arrays and compiler buffer classes are distinct data structures.
