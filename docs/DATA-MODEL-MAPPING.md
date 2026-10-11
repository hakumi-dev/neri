# Data model mapping

The Data generator resolves mappings against the semantic model of the selected
entity unit. Each column maps a public field, field path, or explicit accessor to one SQL column.
Mappings support `Int`, `Bool`, `Float`, `String`, `buffers.ByteBuffer`,
`neri_data.Decimal`, and nullable versions of these types. Portable scalar wrappers
include `Guid`, `DateOnly`, `TimeOnly`, UTC `DateTime`, `DateTimeOffset`, and
`TimeSpan`. `String[]`, `Int[]`, `Bool[]`, and `JsonDocument` use JSON text storage.

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
alternate keys, indexes, concurrency tokens, and value generation. A keyless
dependent may declare a reference-only relationship to a keyed principal's
primary key. Its generated `includeReference_*` loader materializes detached
rows and shared principal references without an inverse collection.

Keyed mappings may use `view` as their query and write target. They retain normal
tracking; the database must support the requested view writes. Schema generation
excludes views, which the application manages separately.

A keyed mapping can declare `table` as its physical write target and `readView`
as its read source. The context reads the authored view through a composed SQL
source and saves tracked inserts, changes, and deletions to the table. The view
exposes the mapped column names. Schema generation manages the table; the
application manages the view. Set writes on this composed read source require a
separate direct-table query.

## Complex fields

Column members may be public field paths such as `home.city` or
`shipping.address.line`. Each intermediate field has an accessible class
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
Intermediate complex fields may be optional. Every optional complex object needs
at least one required mapped leaf that determines its presence. An absent object
stores NULL in all its columns; decoding rejects partial rows whose presence leaf
is NULL while another column is populated. Tracking preserves absence, creation,
replacement, and removal. Nested optional objects use their full paths and own
presence leaves. Objects in separate tables use relationship mappings.

An entity's `owned` array names flattened complex member paths, such as
`"owned": ["shipping.address"]`. These objects have owner identity: one instance
cannot be shared or moved between tracked owners or defining navigations.
Replacement with a fresh instance and optional removal update the owner row;
owner deletion removes its stored values. Context clearing releases ownership.
Ownership histories are bounded and participate in read, save, and transaction
rollback. Ordinary complex values retain value semantics and can be shared.

A separate-table owned collection declares `"ownership": true` on its
collection/reference relation. Every FK component is required and belongs to
the dependent primary key; a typical key is `["ownerId", "localId"]`.
Its identifying key prevents persisted reparenting. Collection changes enroll
new children, reject ambiguous sharing, and delete removed children. Ownership
defaults to client cascade and database `CASCADE`; deleting an owner deletes
tracked children and the database removes unloaded children. Nested ownership
forms an acyclic tree with one defining owner relation per dependent type.

A direct complex column can declare `"json": true`. The generator emits codecs
for its public scalar and nested complex fields, including nullable members,
portable wrappers, and primitive collections. Complex classes require
zero-argument construction and an acyclic tree of depth at most 16, with at most
256 public fields per object. Storage is portable JSON text. Wrong scalar kinds
and missing required members fail decoding. Deep edits are captured as new JSON
text, and JSON scalar path expressions query the stored document.

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

An application-defined conversion uses an object with `storageType`, `encode`,
`decode`, and `initial`. Storage nullability matches the model field. The
accessible functions have signatures `fn(ModelType): Cell`,
`fn(RawRow, String): DecodeResult<ModelType>`, and `fn(): ModelType`.
The encoder and decoder define one-column storage for classes, enums, and other
model values. Decoder failures reject malformed values before publication.

For a converted class used in `.where(field: value)` or a quotation comparison,
the class defines `public static def captureQuotation(value: readonly ModelType?):
quotation.CaptureValue`. The hook returns the same storage scalar as its encoder,
or `Null` for null. Immutable classes can expose readonly comparison operators;
the compiler snapshots captured scalar values when constructing a quotation.
Member queries inside arbitrary converted objects require explicit JSON paths or
separate mapped fields.

An entity may name `snapshotEqual`, an accessible `fn(RawRow, RawRow): Bool`.
The tracker uses it to suppress equivalent snapshots. Captured cells must still
be independent of mutable model objects; generated collection encoders capture
their current contents as JSON text.

## Table-per-hierarchy inheritance

A keyed root can declare `hierarchy` with `strategy: "tph"`, a
`discriminatorColumn`, its own discriminator `value`, and `derivedTypes` entries.
Each entry names an accessible concrete subclass, its distinct `value`,
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

Nested descendants register after their mapped base and inherit its mapped
fields. Roots and intermediate nodes may be abstract; their constructors are
never used for materialization. `ofType_BaseSubtype()` includes its registered
concrete descendants. Subtype navigation fields and subtype store-generated
values require additional contracts. Hierarchy seeds require a discriminator
model and are rejected by the current mapping parser.

`strategy: "tpt"` gives every mapped descendant a distinct `table`. Reads join
all hierarchy tables and choose the deepest concrete node. The root owns shared
identity; table segments write their local columns in one transaction and
propagate generated root keys. Original snapshots retain edits made immediately
after attachment. Base concurrency tokens protect leaf-only updates. Root custom
snapshot comparers are restricted to each segment by holding other values at
their original snapshot.

`strategy: "tpc"` gives each concrete node a distinct `table`; abstract nodes
have no physical table. Reads use a union with virtual discriminators, and the
root tracker selects the concrete write table. Hierarchy keys are assigned and
globally unique across concrete tables; duplicate keys fail materialization.
Root indexes apply to each concrete table. Runtime relationships to polymorphic
roots work without a database FK to a shared root table. Descendant-specific
index declarations, generated values, and complex JSON inheritance combinations
require additional contracts.

## Skip navigations

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
join rows after validating their states. Save-time detectors reconcile edits to
loaded skip arrays, including inverse edits, and enroll new join rows. Incomplete
join baselines reject destructive collection changes.

For an implicit join, omit `join`, `leftForeignKey`, and `rightForeignKey` and
provide `joinTable`. The generator synthesizes a composite-key join model and
both foreign-key relationships. Endpoint keys propagate before the join is
inserted. Hidden join collections belong to the context and are cleared on reset.
Self-referencing joins use distinct forward and inverse collection members.
Their synthesized left and right FK prefixes preserve direction.

## Typed navigation descriptors

Ordinary relationship descriptors use the declared collection and reference
names under `context.navigation.<set>`. For example:

```neri
db.authors.include(db.navigation.authors.books).thenInclude(db.navigation.books.publisher).all()
```

Each descriptor has a typed `NavigationStep<Source, Target>`. The existing
`collectionStep_*` and `referenceStep_*` methods also accept explicit options.
Save-time detectors reconcile mapped collection and reference edits through the
registered relationship policy.

## Portable scalar and JSON storage

`Guid` uses canonical lowercase text. `DateOnly` uses Gregorian dates from year
0001 through 9999. `TimeOnly` and UTC `DateTime` use seven fractional digits;
`DateTime` accepts UTC values ending in `Z`. These immutable wrappers validate
parsing and use canonical text for equality and ordering on both providers.
They use portable text columns rather than PostgreSQL native date or UUID types.

Persisted values, literal defaults, and seed text must already use the canonical
storage form. Decoders reject noncanonical text instead of changing key identity
during materialization. DateTimeOffset literal defaults and seeds require an
explicit authored converter; ordinary mapped values use the validated codecs.

`DateTimeOffset` stores its canonical UTC instant and original offset. SQL
comparisons use the UTC prefix while decoding retains the offset. Built-in
primary, alternate, unconditional unique, and relationship key codecs store
only the UTC prefix and materialize a zero offset, preserving instant equality
for database constraints, tracker identity, and FK fixup. Non-key fields retain
their original offsets. `TimeSpan` stores signed 64-bit ticks as an integer.

Primitive collection codecs encode complete `String[]`, `Int[]`, and `Bool[]`
values as JSON text and reject wrong JSON element kinds. `JsonDocument` validates
and canonicalizes arbitrary JSON at construction. JSON query paths and collection
operations use the provider's JSON expressions.

## Shadow and indexer accessors

A column may specify `type`, `getter`, and `setter` instead of a declared model
field. The accessible callbacks have signatures `fn(Entity): Type` and
`fn(Entity, Type): Void`. Capture, decoding, assignment, and snapshots use these
callbacks; SQL expressions resolve the logical `member` identity. Accessor keys
and store-generated accessor fields require additional contracts.

`ShadowProperties<Entity>` provides a bounded identity-based `Cell` bag for
callback implementations. Binary cells are copied on both writes and reads.
Applications can use the same accessor contract for their own indexers.

## Physical schema facets

Columns support `defaultSql`, `computedSql`, `computedStored`, `checkSql`, and
`collation`. Indexes support descending keys, trusted SQL `predicate` and
`expressions`, and logical-field `include` lists where supported by the provider.
Relations support `deferred` and `databaseOnDelete` with `NO ACTION`, `RESTRICT`,
`CASCADE`, or `SET NULL`. The database delete action is independent of the tracked
`deleteBehavior` policy. Trusted SQL expressions must be bounded single
expressions; model authors are responsible for dialect-specific semantics.

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
