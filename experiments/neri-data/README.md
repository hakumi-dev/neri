# Neri Data query contract prototype

This experiment implements a small query API using ordinary Neri declarations.
`data.hk` is a manually authored mapping fixture for `Customer` and `Order`.
`sample.hk` checks its observable query-plan behavior. The executable builds
SQL-shaped previews and typed parameter values; it performs no database I/O.
Delivery acceptance and remaining work live in [#61](https://github.com/hakumi-dev/neri/issues/61).
Observed results and limitations are recorded in [VERIFICATION.md](VERIFICATION.md).

## Current API

```neri
use neri_data

def main(): Void
  let query = customers().where(customerActive().eq(false))
  let plan = query.where(customerNickname().isNull()).compile()
  let names: Projection<Customer, String> = select<Customer, String>(query, customerNameProjection())
end
```

`Predicate<T>` and field descriptors carry the entity type. `BoolField<Customer>`
offers `eq(Bool)`; `NullableStringField<Customer>` additionally offers `isNull()`
and `isNotNull()`. Integer fields offer equality and ordered comparisons.
Repeated `where` calls conjoin predicates. `orderBy` accepts an integer field
of the same entity and replaces the preceding ordering. `select<T, R>` accepts
a known projection descriptor, retaining its result type until `compile()`.
The module function uses the current generic-function capability.

Omitting `where` leaves the filter absent. `eq(false)` contributes a Bool
parameter. `isNull()` contributes an `IS NULL` predicate without a parameter.
`eq` on nullable text accepts a nonnullable String; the SQL-shaped comparison
uses SQL's three-valued interpretation: a NULL row does not satisfy equality.
The fixture records this operation but does not evaluate rows.

Values are captured when predicate methods are called. Chaining appends their
parameters in predicate order; compilation does not invoke callbacks. Previews
use `?` placeholders and fixed fixture identifiers. They are inspection data,
not PostgreSQL statements: a provider must supply its placeholder syntax and
validate mappings and decoded rows. `QueryPlan` is an editable inspection
snapshot and erases the result type. There is no typed materialization contract
at this terminal boundary yet.

The related entities have an explicit primary-key/foreign-key descriptor.
The relationship fixture describes a mapping; it does not perform a join or
load related entities. Constructors are internal to the compilation boundary;
this experiment is not an isolation boundary against code in that same unit.

## Reproduce

Run from the Neri repository root with its built toolchain:

```sh
scripts/neri.sh check --project experiments/neri-data --unit library > /tmp/neri-data-library.ir
scripts/neri.sh run --project experiments/neri-data --unit sample
scripts/neri.sh run --project experiments/neri-data/verification
```

`editor/probe.hk` is a separate executable consumer for manual Rider checks.
The verification project checks the language-service contract against the
fixture source. Its intentionally invalid executable units are compiler-negative
probes, not runnable examples.

## Research basis

The following are primary sources, verified on 2026-09-12. Their results motivate
the experiment; this implementation does not inherit their formal proofs.

- Cheney, Lindley and Wadler, [A Practical Theory of Language-Integrated Query](https://www.pure.ed.ac.uk/ws/portalfiles/portal/18383912/Cheney_Lindley_ET_AL_2013_A_practical_theory_of_language_integrated_query.pdf),
  ICFP 2013, DOI `10.1145/2500365.2500586`: quotation and normalization support
  composable queries, with conditions guaranteeing translation to one SQL query.
  This fixture has a much smaller operation set and uses explicit descriptors.
- Cheney, Lindley, Radanne and Wadler, [Effective Quotation](https://arxiv.org/abs/1310.4780),
  PEPM 2014, DOI `10.1145/2543728.2543738`: relates quotation-based and
  effect-based approaches through calculi and translations. It does not establish
  the ergonomics or editor behavior of this API.
- Microsoft, [Expression trees](https://learn.microsoft.com/en-us/dotnet/csharp/advanced-topics/expression-trees/):
  an expression tree represents code as inspectable data. An ordinary Neri
  `fn(T): R` callback does not provide such a representation. Explicit projection
  descriptors keep this prototype's supported operations inspectable.
- Microsoft, [Client vs. server evaluation in EF Core](https://learn.microsoft.com/en-us/ef/core/querying/client-eval):
  unsupported translation outside the top-level projection can fail at runtime.
  Static host-language typing alone does not establish SQL translatability.
- PostgreSQL, [Comparison functions and operators](https://www.postgresql.org/docs/18/functions-comparison.html):
  NULL comparisons and `IS NULL` are distinct operations. The fixture exposes
  separate nullable-field methods rather than treating omission as SQL NULL.

Autocompletion quality is an empirical acceptance criterion. The papers do not
provide evidence that a particular Neri signature appears correctly in Rider.
