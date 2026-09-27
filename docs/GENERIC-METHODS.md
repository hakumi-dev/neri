# Generic methods

A method can declare type parameters independently of its containing class.
The class supplies receiver types; the method supplies types chosen for each
call. This lets a query for a fixed entity type select fields of different
scalar types without separate method names or an erased public selector API.

```neri
class Selector<T>
  public readonly def identity<U>(value: U): U
    return value
  end
end

let selector = new Selector<Int>()
let inferred = selector.identity("text")
let explicit = selector.identity<String>("text")
```

Methods on ordinary classes can also be generic. Explicit type arguments must
match the method's type-parameter count. Inference uses call arguments and
the existing contextual type machinery. A quotation's known parameter type can
come from the containing class while an annotated quotation result determines
the method type argument. An unresolved type argument produces a diagnostic.

Specializations retain the receiver class and its type arguments. Two instances
of different specialized classes therefore do not share an incompatible method
body merely because the method type arguments match. Ordinary instance/static
call rules, visibility, and readonly receiver restrictions still apply.

Method type parameters must have distinct, non-reserved names. A method type
parameter that shadows a containing class parameter is rejected explicitly.
Neri does not use C#'s warning-and-shadowing rule. Generic constructors, abstract
methods, overrides, method annotations, and labels parameters remain unsupported.
Generic class inheritance and separately
compiled generic template ABIs retain the existing language restrictions.
Generic methods are called through their receiver; extracting one as a function
value reports `NR220`. Generic module function values remain a separate feature.

## References

Primary sources verified on 2026-09-23:

- Microsoft, [generic methods](https://learn.microsoft.com/en-us/dotnet/csharp/programming-guide/generics/generic-methods):
  distinguishes class and method type parameters and describes inferred and
  explicit calls. Neri retains its own inference and declaration rules.
- Rust Compiler Development Guide,
  [monomorphization](https://rustc-dev-guide.rust-lang.org/backend/monomorph.html):
  describes generating concrete code for generic instantiations. Neri extends
  its existing specialization model to methods; this reference does not imply
  shared compiler machinery or an identical type system.
- Siek and Lumsdaine,
  [A Language for Generic Programming in the Large](https://arxiv.org/abs/0708.2255),
  2007: motivates explicit generic requirements and reusable library interfaces.
  Its modular checking and separate compilation properties belong to language G;
  this implementation does not claim those results for Neri.
