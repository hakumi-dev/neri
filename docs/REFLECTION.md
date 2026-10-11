# Compile-time field reflection

`compile for field in reflect.fields<T>()` expands its body once for each public
instance field of a concrete class. Base-class fields come first, followed by
each derived class's fields in declaration order. Generic specialization resolves
`T` and each field's type before binding the expanded statements.
`compile` is a reserved keyword. Its statement form is `compile for`.

```neri
class Input
  public name: String = "Ana"
  public age: Int = 28
end

def describe<T>(value: T): String
  var text = ""

  compile for field in reflect.fields<T>()
    let item = field.read(value)

    text = text + field.name + ": " + (item as String) + "; "
  end
  return text
end
```

The descriptor exposes three constant strings: `name`, `typeName`, and
`declaringType`. `typeName` is the resolved field type; `declaringType` identifies
the class that declares the field, including its concrete generic arguments.
`read(value)` emits a direct field access with the declared type. Its argument
evaluates once per call and must be a non-optional instance of the declaring
class or a subclass. Reading through a readonly receiver preserves readonly
member rules.

Each expansion has its own local scope. Nested compile-time field loops and
closures can use descriptors from their enclosing expansion. An empty class
produces no statements. Private, protected, and internal fields are omitted.
Methods, properties, enum cases, represented types, and resource classes are
outside this field-reflection contract.

Descriptors are compile-time metadata. They cannot be stored, returned, or passed
as runtime values. The reflected field collection is valid only as the source
of `compile for`. Ordinary `for` statements retain their array semantics.
`break` and `continue` require an ordinary loop inside the expansion; they cannot
target a compile-time loop. Runtime conditions retain ordinary type checking in
both branches.

Field reflection reports `NR285` for invalid metadata operations, incompatible
receivers, unsupported reflected types, and expansion limits. The compiler bounds
field expansion to 4096 entries per binding pass and inheritance traversal to
256 classes. Other expression and assignment errors retain their regular
diagnostics with the concrete field type.

The executable [reflection contract](../tests/contracts/reflection.hk) includes
a generic JSON serializer using the existing JSON writer, typed field reads,
inherited fields, generic classes, and closure captures.

## Technical references

[P2996R13, Reflection for C++26](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p2996r13.html)
describes compile-time reflection values, member queries, and typed member access.
[P1306R5, Expansion Statements](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p1306r5.html)
describes repeated instantiation of a block over compile-time elements.
These WG21 design proposals provide precedents for the separation between metadata
and runtime values and for binding each expansion independently.

Neri defines a narrower field-only interface with public-field visibility and its
own `compile for` syntax. Its control-flow restrictions and expansion limits are
Neri contracts. The referenced proposals do not establish the correctness of
Neri's implementation; compiler contract tests validate its supported behavior.

## Checked managed reference casts

`reference.cast<To, From>(value)` returns `To?`. It returns the same object when
its runtime class is `To` or a subclass of `To`, and returns null otherwise.
The cast preserves reference identity and checks the runtime class ancestry.
Both type arguments are non-optional, mutable managed class types. Resource
classes, represented values, and function types are excluded.

```neri
let derived = reference::cast<SpecialRecord, Record>(record)

if derived != null
  consume(derived.extra)
end
```

Runtime type descriptors carry their declared base type as a size-guarded tail
extension. Existing descriptor fields keep their offsets. Descriptors created
without the ancestry extension support exact-class matches.
