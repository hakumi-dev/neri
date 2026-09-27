# Typed quotations

`quote fn(T): R` describes an inspectable expression with parameter type `T`
and result type `R`. The compiler binds its body with the ordinary member,
scope and type rules. The `quotation` library provides the inspection API.

```neri
use quotation

class Customer
  public active: Bool = false
  public id: Int = 0
end

def activeAfter(minimum: Int): quote fn(Customer): Bool
  return quote do |customer|
    return customer.active && customer.id > minimum
  end
end
```

The return signature supplies the parameter and result types. An explicit
quotation without an expected signature supplies both annotations:

```neri
let expression = quote do |customer: Customer|: Bool
  return customer.active
end
```

A parameter with a quotation signature also gives a trailing block its expected
type:

```neri
def inspect<T>(expression: quote fn(T): Bool): readonly Tree
  return expression.tree()
end

let tree = inspect<Customer>() do |customer|
  return customer.active
end
```

Quotation signatures preserve parameter and result type identity. Generic
inference can recover these types from a quotation argument. Their values expose
`tree()` and can be stored and passed between functions. Invoking a quotation as
a function, constructing its wrapper with `new`, and assigning an incompatible
quotation signature produce compile-time diagnostics.

## Expression and capture contract

The body contains one `return` expression. Supported bound operations are
parameter reads, field reads, scalar captures, `Int`, `Float`, `Bool`, `String` and null
literals, primitive comparisons, Boolean conjunction and disjunction, Boolean
negation, numeric negation and numeric `+`, `-`, `*` and `/` on operands of the
same arithmetic type. `String + String` is represented as concatenation only
for the standard `String.concat` operator. Ternary expressions require a Bool
condition and compatible branch types, including a type with its optional
form or null. Field nodes retain the declaring field's resolved identity and
the static type at that occurrence. A null guard can refine an optional field
path in its present branch, so that occurrence has the required type while
the null-test occurrence retains its optional type. Parameter and capture nodes
refer to indexed slots.

Quotation bodies use the ordinary compiler's guarded member-path rules.
Consumers still validate their own supported expression and mapping contracts;
a narrowed node in an untyped, manually constructed Tree is not proof that a
field or intermediate object is present.

Captures use immutable bindings and scalar `Bool`, `Int`, `Float`, `String` or null values.
Each captured binding is read once when the quotation is constructed. Repeated
reads in the expression refer to the same capture slot. Optional `Bool?`, `Int?`,
`Float?` and `String?` values use the standard `quotation::captureBool`,
`quotation::captureInt`, `quotation::captureFloat` and `quotation::captureText` helpers, preserving either
the scalar value or `CaptureValue.Null` without a conversion. Field packs use
the same validated capture ABI. A different optional type is rejected.

Standard `String` equality, inequality and concatenation operator syntax is
represented by dedicated nodes. Direct method calls and custom overloaded
operators produce `NR276`, as do unsupported operations such as indexing and
casts. Ordinary type and member errors retain their standard diagnostics.
Operation diagnostics point to the bound operation's source span.

## Inspection

`expression.tree()` returns a readonly `quotation::Tree`. It exposes the root
index, node and capture counts, parameter types and result type. `nodeAt`,
`captureAt` and `parameterTypeAt` return null for an invalid index.

Each `Node` exposes its closed `NodeKind`, static type, three child indices,
parameter or capture index, field identity and literal payload. Binary nodes use
`first()` and `second()`; `Conditional` uses `first()` for its condition,
`second()` for its true branch and `third()` for its false branch. Other nodes
leave `third()` at `-1`. `CaptureValue` has `Bool`,
`Int`, `Float`, `Text` and `Null` cases. Float literals use `FloatLiteral` and
`Node.floatValue()`. Tree arrays remain private. A manually constructed
`Tree` is an untyped data value; the compiler constructs typed quotation wrappers
from checked expressions.

The compiler validates the library constructor and alternative-case signatures
before constructing quotation data. Case tags are resolved from case names.
An incompatible quotation library produces `NR276` before native lowering.

## References

[A Practical Theory of Language-Integrated Query](https://doi.org/10.1145/2500365.2500586)
and [Effective Quotation](https://arxiv.org/abs/1310.4780) motivate explicit typed
representations for language-integrated queries. Their normalization results
depend on the calculi and restrictions stated in those papers. Neri's current
quotation representation provides a compiler-checked inspection boundary; it
does not establish those normalization results for Neri.

[C# expression trees](https://learn.microsoft.com/en-us/dotnet/csharp/advanced-topics/expression-trees/)
provide a reference for contextual conversion of a typed lambda into inspectable
data. Neri's supported expression subset and library API are defined above.
