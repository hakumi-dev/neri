# Named argument processing

Named arguments use ordinary declared parameter names. The language grammar and
call rules are specified in [Language](LANGUAGE.md#functions-modules-and-classes).
This capability is shared by application and library code.

The frontend represents `label: expression` as a `NamedArgument` syntax node,
with a label range, colon range and expression child. Missing values have a
`MissingExpression` node and a parse diagnostic. The parser retains source order.
Arrays and native intrinsic expressions have separate argument-list rules.

The semantic mapper resolves each supplied argument to one parameter of the
selected callable. It records both the source-to-parameter relation and supplied
parameter slots, checking label existence, uniqueness, ordering and required
arguments. Generic calls use the template's parameter list before specialization.
Inference and contextual checking then use the mapped parameter's type pattern.
No entity, table, field or ORM identifier participates in these rules.

Bound arguments retain their source ordering and parameter indices. Lowering
evaluates their expressions once and arranges the resulting SSA values for the
call ABI. Default adapters identify the supplied parameter set, receive those
values in parameter order, and evaluate omitted defaults in declaration order.
An omitted default can therefore refer to an earlier supplied or defaulted value.
Direct, virtual and base-constructor calls share that placement rule.
Named calls to unsafe and C ABI declarations supply every parameter explicitly;
the binder reports `NR275` when such a call requests a default adapter.

The binder records parameter-label occurrences for semantic navigation. Its
completion sites expose remaining labels and parameter types to both LSP and
retained sessions. An existing label completes its name; an argument start can
insert `name: ` alongside ordinary expression suggestions. Signature help uses
bound parameter indices instead of interpreting comma position as parameter
position. Function values retain their positional callable-type contract.
An incomplete named value is recovered from the parser's `MissingExpression`
node for signature help. Generic parameter occurrences use their template's
source identity while hover retains the concrete inferred type.

Direct LSP and retained-session contracts exercise this semantic surface.
Hover over a label whose value is still missing uses the same structural
recovery as signature help. A UTF-16 position map preserves the label's original
range and documentation provenance across the repaired query and other sources.
Editor integration acceptance is tracked in [#62](https://github.com/hakumi-dev/neri/issues/62)
and [#55](https://github.com/hakumi-dev/neri/issues/55).

Argument omission selects a declared default expression. SQL null handling and
filter omission belong to the query library's types and generated signatures.

## Sources

These primary sources were checked on 2026-09-12:

- Dunfield and Krishnaswami, [Bidirectional Typing](https://arxiv.org/abs/1908.05839),
  ACM Computing Surveys 54(5), 2021, DOI `10.1145/3450952`. The survey distinguishes
  synthesis of types from checking against expected types. Neri uses that
  distinction when arguments supply inference evidence and contextual arguments
  consume resolved parameter types.
- Dunfield and Krishnaswami, [Complete and Easy Bidirectional Typechecking for
  Higher-Rank Polymorphism](https://arxiv.org/abs/1306.6032), ICFP 2013,
  DOI `10.1145/2500365.2500582`. The paper presents declarative and algorithmic
  systems with formal soundness and completeness results. Those results apply
  to its calculus; this feature neither adds higher-rank polymorphism nor claims
  those proofs for Neri's existing generic inference algorithm.
- Microsoft, [C# specification, expressions](https://learn.microsoft.com/en-us/dotnet/csharp/language-reference/language-specification/expressions),
  sections 12.6.2.2, 12.6.2.3 and 12.6.3. Argument correspondence precedes type
  inference, and runtime argument evaluation follows source order. Neri's
  positional-before-named rule is explicit in its own language contract; it
  does not inherit C#'s overload resolution or argument-mixing rules.

The implementation's guarantees are checked by the parser and semantic/editor
contracts in the root manifest and the execution contract under
`tests/value-contracts/named-arguments`. The execution contract observes ordering
and defaults, generic contextual inference, constructors, virtual dispatch and
declared intrinsic calls.
