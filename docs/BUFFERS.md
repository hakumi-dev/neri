# Growing buffers

`use buffers` provides bounded byte and text accumulation and an ordered generic
value collection. The growing buffers expand geometrically, so appending values
does not copy the complete prefix on every operation.

`ByteBuffer.create(limit, initialCapacity = 16)` accepts at most 128 MiB and
returns null for a negative or larger limit or a negative initial capacity. The
initial capacity is capped at the limit, including when the default exceeds a
small limit. `append`, `appendBytes`, `appendSlice`
and `appendText` return false without mutation when the input range is invalid or
the operation would exceed the limit. Capacity doubles as needed and stops at the
declared limit. Appending one byte has amortized constant cost; capacity changes
are observable through `capacity()` when growth needs to be measured.
Geometric expansion is the dynamic-table method described by
[Cormen, Leiserson, Rivest, and Stein](https://mitpress.mit.edu/9780262046305/introduction-to-algorithms/):
the total existing bytes copied across uncapped expansions is less than twice
the final capacity.

`copyWithin(target, start, count)` copies inside the current content. Both ranges
must already be within the logical length. Overlapping ranges behave like
`memmove`; the logical length does not change. `byteAt` returns null outside the
logical content. This matches the
[POSIX `memmove` overlap contract](https://pubs.opengroup.org/onlinepubs/9799919799/functions/memmove.html).

`snapshot()` returns a distinct byte array containing the logical content. Later
buffer writes do not change an earlier snapshot. `text()` decodes a snapshot as
UTF-8 with `host::stringFromBytes` and returns null for invalid UTF-8. Accepted
text follows [RFC 3629](https://www.rfc-editor.org/rfc/rfc3629).

`TextBuffer.create` applies the same byte limit and geometric growth to UTF-8 strings.
`append(value)` is atomic and `snapshot()` returns the accumulated string. Text
length and capacity are measured in bytes.

`Values<T>.create(limit)` retains at most `limit` repeated values in insertion
order. `add` returns false without mutation at the limit. `count` and `each`
provide the current collection contract, and iteration is linear. `each` visits
the values present when iteration starts; values added by its callback remain in
the collection for later iterations. Values is not
a keyed map or set and performs no deduplication.
