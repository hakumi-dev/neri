# Language reference

Neri compiles typed source to native code. Debug and Release preserve the same
arithmetic, null, bounds, evaluation-order, and lifetime rules.

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

`use console` provides terminal input and output: `console.print(value)` writes
without a newline, `console.println(value)` appends a newline, and
`console.read()` reads a line as a string. Output accepts strings and numeric
values. End of input produces an empty string. Output is flushed after each call.

## Values and variables


| Type | Contract |
|---|---|
| `Int` | Signed 64-bit integer; checked addition, subtraction, multiplication and negation. |
| `Float` | IEEE-754 binary64, including infinities, NaN and signed zero. |
| `Byte` | Unsigned 8-bit integer. |
| `Bool` | Boolean logic and equality. |
| `String` | Immutable UTF-8, concatenation and ordinal value equality. |
| `Void` | No returned value. |
| `T[]` | Homogeneous fixed-length array, checked indexing, `Length`, and `for` iteration. |
| `T?` | Explicit optional value. |
| Class | Managed reference to an instance. |

Integer division truncates toward zero; division by zero and minimum Int divided
by -1 panic. Float arithmetic follows IEEE-754 without fast-math.
Float equality considers two NaNs equal; ordered comparisons with NaN are false.
Numeric casts are explicit: `Int`/`Float`, checked `Int` to `Byte`, and lossless `Byte` to `Int`.
Float-to-Int truncates and panics for unrepresentable values. Numeric `as String`
conversions are locale-independent. Arrays and objects have no equality operator.

`let` and parameters are immutable bindings; `var` permits reassignment. Binding
immutability does not freeze the fields of an object. An explicit annotation
supplies the element type for `let values: Int[] = []`. `let values = []` fails
because there is no element type to infer.

Only `T?` accepts `null`. Proven checks such as `x != null` refine optional locals
on the corresponding control-flow path. Access requires that refinement.
`T?[]` and `T[]?` differ. `Void?` and repeated optional suffixes are invalid.

## Functions, modules and classes

Functions declare parameter and return types. Trailing default arguments,
recursion and forward calls are supported. A program defines exactly one
non-namespaced top-level `main(): Void` with no parameters. Non-Void functions
return on every statically recognized path. Overloading is outside this surface.

`if`, `while`, and `for` have lexical scopes. A `for` element binding is immutable.
`break` and `continue` require an enclosing loop.

A single-line assignment, call, `return`, `break`, or `continue` may use a
postfix condition: `result = result + value if value != null`. The condition
is checked before the action and refines optional types inside that action.
Postfix conditions have no `else`, `end`, or chained modifier. Declarations and
multiline actions use a block instead. Block `if` bodies begin on the next line;
`else` and `end` begin separate lines. `else if` chains remain supported.

All supplied source files contribute to one compilation module. `namespace`
applies to subsequent declarations; `use` exposes a namespace throughout the
module. `use http`, `use terminal`, and `use clock` load their bundled libraries;
other namespaces do not load files. Duplicate or ambiguous declarations are errors.
Module scope contains only namespace/use directives and function/class declarations;
other tokens produce a parse diagnostic.

Classes have single inheritance. Classes default to `internal`, fields to
`private`, and methods to `public`. `internal` is module visibility, `private`
is declaring-class visibility, and `protected` includes derived classes.
Instance methods dispatch virtually; exact name and signature override a base
method. `@override` asserts that relationship. `def static` declares a method
without a receiver. `super.method()` dispatches directly to the base method.
Omitted arguments use defaults from the statically resolved declaration; supplying
those defaults preserves virtual dispatch to the receiver's implementation.

Construction initializes base classes before derived classes. `init` is not
inherited. An explicit `super(args)` starts a derived initializer when the base
requires arguments. A valid zero-argument base call is implicit. Inherited fields
cannot be redeclared.

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
compiled once per compilation module. Unused generic bodies are checked when
specialized. Type parameters accept safe value types; `Void`, raw pointers and
unresolved types cannot be type arguments. The entry point and C ABI imports have
concrete signatures.

The current generic surface consists of module functions and classes with their
own fields and methods. Method-level type parameters, generic class inheritance,
interface constraints and higher-kinded types are outside this surface. Templates
are supplied as source files in the same compilation invocation, including across
namespaces. A compiled specialization is concrete; it is not a separately
importable generic template or a package ABI promise.

Compilation permits 128 distinct generic specializations, 32 nested class
instantiations, 64 levels of written type nesting, and canonical type arguments
of at most 1024 UTF-8 bytes. Ordinary recursion reuses the same specialization;
recursion that creates an unbounded sequence of new types reports `NR222`.
`NR220` reports invalid generic declarations or type-argument counts; `NR221`
reports arguments that cannot be inferred or used as safe value types.

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

`fn(parameters) ... end` creates a closure. An expected function type supplies
omitted parameter and return annotations in arguments, returns, annotated
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
  let pointSize = native.sizeOf<Point>()
  let alignment = native.alignOf<EventStorage>()
  let yOffset = native.offsetOf<Point>("y")
end
```

`sizeOf` and `alignOf` take one type argument and no value arguments;
`offsetOf` additionally takes a literal field name. Results are byte counts of
type `Int`, and the queries are safe operations. Layouts target the supported
64-bit native platforms, are limited to one GiB and 128 nested types, and use
four-byte alignment for 32-bit numbers and eight-byte alignment for 64-bit
numbers and pointers.

Native records are value types: copying a record copies its inline fields.
`stackalloc Record[count]` and `native.allocZeroed<Record>(count)` provide
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
    let sample = native.allocZeroed<Sample>(1)
    sample.position[0] = 1.5 as Float32
    sample.code = 42 as UInt32
    let codeAddress = &sample.code
    var copy = *sample
    copy.code = 7 as UInt32
    native.free(sample)
  end
end
```

Union members share storage and have no automatic active-member tag. Native
code must establish which member is valid before reading it. C ABI declarations
exchange records through typed pointers.

### Raw memory operations

Raw pointer operations require `def unsafe` or an `unsafe ... end` block. `T*`
is non-null; `T*?` requires a null check before access. Raw pointers do not retain
managed allocations. Address-of applies to mutable unmanaged locals.

`stackalloc T[count]` allocates uninitialized storage for the function lifetime.
`native.alloc<T>` and `native.allocZeroed<T>` allocate manually owned storage.
`native.realloc` consumes the previous allocation and preserves its common prefix;
`native.free` releases native storage. Stack and borrowed storage cannot be freed
or reallocated. Invalid pointer access inside unsafe code may have undefined
behavior; safe wrappers must restore the language invariants before returning.

`borrow values as pointer ... end` exposes an unmanaged array payload for a
lexical scope. The owner remains alive and its address stable. The pointer cannot
escape the scope or acquire ownership. `@cabi("symbol")` on an empty unsafe
function declaration imports a C symbol with supported scalar and pointer types.

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
@cabi("cos")
def unsafe cosine(value: Float): Float
end
```

Neri IR preserves library declarations through the `native-libraries-v1`
feature in transport 1.2. Modules without this feature retain transport 1.1.

The runtime [ABI and collection contract](ABI.md) defines roots and allocation
boundaries. Bounds and arithmetic failures panic with exit status 70. Compile
errors produce diagnostics and prevent artifact emission.

## Tooling

`check` validates and emits canonical NIR by default. A source file without a
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
