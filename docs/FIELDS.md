# Typed field arguments

`fields of T` is a typed collection of supplied field values for a declared class
`T`. A final `labels` parameter accepts named arguments whose names and types
come from that class's public instance fields, including inherited fields.

```neri
use fields

class Customer
  public active: Bool = false
  public nickname: String?
end

def collect<T>(target: T, labels values: fields of T): fields of T
  return values
end

let values = collect(new Customer(), active: false, nickname: null)
let entries = values.pack()
```

Ordinary arguments and explicit type arguments determine `T` through normal
generic inference. The compiler resolves each supplied field against that type
and checks its value using the field's declared type. Field argument completion
and navigation use those same declarations.

## Call contract

A safe ordinary function or method can declare one final `labels` parameter,
with a `fields of T` type and no default expression. Ordinary parameters retain
their named and positional argument rules and take precedence when a field has
the same name. The pack parameter's internal name remains a local variable in
the callable body. Calls supply its entries by field name.

Duplicate labels, inaccessible or unknown fields, incompatible values and
positional pack arguments produce compile-time diagnostics. Current entries
support `Bool`, `Int`, `String` and their optional types. Other fields can remain
on the entity; supplying one as an entry requires additional representation
support.

An omitted field contributes no entry. `false` contributes a Boolean entry, and
`null` contributes a null entry for an optional field. An empty call contributes
an empty pack. All argument expressions, including ordinary arguments interleaved
with field arguments, evaluate once in source order.

The entity type is invariant: a pack for one entity cannot be assigned to a pack
for another entity. Typed pack values can be stored, returned and passed as
ordinary values. The compiler constructs them through checked labeled calls.
`(fields of T)?` denotes an optional pack.

## Inspection

`values.pack()` returns a readonly `fields.Pack`, exposing `entityType()`,
`count()` and checked `entryAt(index)`. Each entry exposes the resolved declaring
field identity, declared type and a readonly `quotation.CaptureValue`. Entries
retain their source order. The underlying arrays remain private.

Public `fields.Entry` and `fields.Pack` constructors create untyped inspection
data. Typed wrappers require compiler validation. Incompatible library
constructors or scalar capture functions produce `NR277` before native lowering.

## References

[First-class labels for extensible rows](https://www.microsoft.com/en-us/research/wp-content/uploads/2016/02/fclabels.pdf)
and [Extensible records with scoped labels](https://www.microsoft.com/en-us/research/wp-content/uploads/2016/02/scopedlabels.pdf)
provide background on relating labels to static types. Neri derives labels from
declared nominal classes and rejects duplicate call labels. These rules and the
scalar inspection representation are Neri engineering contracts; the papers'
row calculi do not establish their correctness automatically.
