# Data model mapping

The Data generator resolves mappings against the semantic model of the selected
entity unit. Each column maps an existing public scalar field to one SQL column.
Mappings support `Int`, `Bool`, `Float`, `String`, `buffers.ByteBuffer`,
`neri_data.Decimal`, and nullable versions of these types.

## Read-only query sources

A keyless mapping declares `"keyless": true`, omits `key`, and names either a
`table` or a `view`. A view replaces the `table` property:

```json
{
  "type": "model.Summary",
  "set": "summaries",
  "view": "record_summary",
  "keyless": true,
  "columns": [{"member": "count", "column": "record_count"}]
}
```

The generated query supports normal reads, projections, and relational stages.
Its mapping is read-only and its query has no tracker. Tracking and set writes
reject the source. Generated graph enrollment excludes keyless types, and schema
generation and migration snapshots include only managed keyed tables. The
application creates and manages the query source separately.

Keyless mappings accept scalar columns and defaults. They omit primary and
alternate keys, indexes, concurrency tokens, value generation, and relationships.

## Required complex fields

Column members may be public field paths such as `home.city` or
`shipping.address.line`. Each intermediate field has a required accessible class
type with zero-argument construction. The root entity supports zero-argument
construction whenever it contains mapped complex fields. Each terminal field has
one of the supported scalar types.

```json
{
  "type": "model.Customer",
  "set": "customers",
  "table": "customers",
  "key": "id",
  "columns": [
    {"member": "id", "column": "id"},
    {"member": "home.city", "column": "home_city"},
    {"member": "work.city", "column": "work_city"}
  ]
}
```

Decoding creates each mapped complex object and assigns its scalar columns.
Tracking captures scalar values by column, applies changes to the existing nested
objects, and keeps repeated uses of the same complex type distinct by their full
member paths. Schema field identities qualify nested values by the root type and
member path. Quotation predicates and projections resolve the complete semantic
path. Label-based filters and relationship keys address direct scalar fields.

This mapping gives complex values independent columns in the owner's table.
Intermediate complex fields are required; terminal scalar fields may be nullable.
Ownership relationships, collection values, optional complex objects, JSON
storage, and complex objects in separate tables require their own mapping models.

## Global query filters

An entity's `queryFilter` names an accessible zero-argument function in the mapped
unit that returns `quote fn(Entity): Bool`. The generated context evaluates this
factory while creating its root query and installs the quotation as a global
filter. Regular entity and keyless queries support this option.

```neri
def activeCustomers(): quote fn(Customer): Bool
  return quote do |customer: Customer|: Bool
    return customer.active
  end
end
```

Set `"queryFilter": "model.activeCustomers"` on the entity mapping. Derived query
stages preserve the filter. `ignoreQueryFilters()` suppresses the global filter
while keeping predicates explicitly added by the application.

`queryFilters` accepts named factories, such as
`[{"name":"tenant","factory":"model.tenantCustomers"}]`.
Each name is unique within its entity and each factory has the same typed
zero-argument signature. `ignoreQueryFilters(["tenant"])` suppresses the selected
named filters while preserving other named filters and the unnamed `queryFilter`.

## Boolean storage conversion

`"conversion": "boolToInt"` on a `Bool` or `Bool?` column stores false as zero and
true as one. Nulls remain null. Decoding rejects integers outside this range.
Defaults and schema snapshots describe the integer storage column, while the
entity keeps its Boolean field type.

Generated trackers capture integer values. Label filters bind integer values;
quotation expressions and scalar projections normalize the stored column to a
Boolean SQL expression. Set updates encode assigned Boolean values back to
integer storage. The conversion applies on both SQLite and PostgreSQL.

This built-in conversion covers Boolean values and nullable Boolean values.
Application-defined converters and other storage type pairs require additional
conversion contracts.

## Table-per-hierarchy inheritance

A keyed root can declare `hierarchy` with `strategy: "tph"`, a
`discriminatorColumn`, its own discriminator `value`, and `derivedTypes` entries.
Each entry names an accessible concrete direct subclass, its distinct `value`,
and its additional mapped `columns`. The root set owns the shared table and key;
subtypes use the root set rather than separate sets.

The generated root decoder selects the concrete model type from the discriminator
and rejects unknown values. Subtype columns are nullable in the shared schema,
while their declared field types remain required when decoding that subtype.
Capture writes the discriminator and subtype fields and writes null for sibling
fields. The root tracker observes subtype edits and preserves one identity per
hierarchy key.

`ofType_Subtype()` on the generated context returns a typed `Relation<Subtype>`.
Its SQL restricts the discriminator, and buffered and asynchronous reads resolve
materialization through the root tracker before returning the checked subtype.
An existing identity with a different subtype fails before enrolling additional
rows. Predicates, projection, ordering, and joins use the subtype's mapped fields.

This contract supports a root and direct concrete subclasses. Nested hierarchy
registration, subtype navigations, subtype store-generated values, TPT, and TPC
require additional contracts. Hierarchy seeds require a discriminator-aware seed
model and are rejected by the current mapping parser.

## Skip navigations through explicit joins

`manyToMany` declares a name, `left`, `right`, and `join` mapped types, the join's
`leftForeignKey` and `rightForeignKey` tuples, and `leftCollection` and
`rightCollection` skip fields. Both endpoint fields are public arrays of the
opposite endpoint type. The two ordinary relations through the join must already
declare collection and reference members.

The join supports zero-argument construction and flat mapped fields. Its key
comes from endpoint foreign keys or store generation. Generated factories assign
the foreign keys and references; generated enrollment callbacks enroll the graph
and register write links so generated endpoint keys reach the join before saving.

The generated context exposes `include_Name`, `include_NameAsync`, `link_Name`,
and `unlink_Name`. Loading publishes the skip collection and its inverse after
both navigation stages succeed. Linking updates both endpoints' skip and join
collections. Unlinking requires a loaded join collection and removes tracked
join rows after validating their states. Self-referencing skip directions and
implicit synthesized join entities require additional mapping contracts.

## Model seed rows

An entity's `seeds` array contains objects whose keys name mapped scalar members,
including flattened member paths. Every row supplies the complete assigned
primary key and all required fields without defaults. Optional fields and fields
with defaults may be omitted. Scalar values use their model JSON types; Decimal
uses an exact decimal string, and binary values use `{"hex":"00ff"}`.
Boolean storage conversion also applies to seed values.

Schema emission and migration snapshots preserve logical field identities and
typed values, including null and binary data. Migration source preserves
`InsertData`, `UpdateData`, and `DeleteData` operations and their parameters.
Rows are bounded to 1000 per entity and binary seed values to 1048576 bytes.
The seed migration dependency rules are described in [seed data](DATA-SEEDING.md).
