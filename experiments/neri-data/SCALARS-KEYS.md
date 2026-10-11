# Exact scalars and ordered keys

Mapped fields accept `buffers.ByteBuffer` and `neri_data.Decimal`, including their
optional forms. SQLite stores buffers as BLOB and exact decimals as canonical
TEXT. Binary decoding copies every byte before the native statement advances;
empty BLOB and SQL NULL are distinct. Capturing a tracked buffer copies its
contents so later buffer edits can be detected and saved.

`Decimal.parse(text)` accepts an optional sign, integer digits, and an optional
fraction, with at most 4096 digits. It returns null for malformed input. Decimal
values have a canonical representation: redundant leading integer zeros,
trailing fractional zeros, and negative zero are normalized. `Decimal.zero()`,
`text()`, `equals()`, `compare()`, `add()`, `subtract()`, and `negate()` preserve
base-ten precision. Addition and subtraction return null if the result exceeds
the digit bound. Required decimal database cells must contain canonical text;
nullable cells also accept SQL NULL. Mapping defaults use JSON strings such as
`"default": "9007199254740993.01"`.

`Query.whereBinary(column, value)` and `Query.whereDecimal(column, value)` bind
copied binary bytes or canonical decimal text. Decimal SQL equality uses this
canonical representation. Numeric ordering and arithmetic on the TEXT storage
representation require a provider-specific numeric expression; lexical text
ordering does not implement numeric decimal ordering.

An entity's `key` is a member name or an ordered array of required `Int` and
`String` members. Assigned composite identities use type tags and byte lengths
for each component. Tracked updates and deletes include every key component;
changing any primary key component produces a validation failure. Generated
keys use a single required `Int` member.

`alternateKeys` contains named unique key definitions:

```json
{"name": "account_external", "members": ["tenant", "externalId"]}
```

Each member must be required and have a key-compatible scalar type. Relations
accept ordered `key` and `foreignKey` arrays of equal length. The principal tuple
must match its primary key or a declared unique key. Foreign key components have
the corresponding scalar types and share nullability. An optional tuple is NULL
only when every component is NULL; partial NULL tuples fail loading validation.

Generated schema columns retain composite primary key positions and foreign key
column tuples. Includes, references, relationship setters, batched predicates,
and filtered include partitions use complete tuples. Navigation ordering appends
all dependent primary key columns for a stable tie-break order.

The [expanded scalar fixture](expanded-scalars/mapping.json) combines a composite
primary key, composite alternate key, composite navigation, exact decimals and
nullable binary buffers.
