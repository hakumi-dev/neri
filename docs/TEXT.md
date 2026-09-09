# Text and Unicode scalars

`String` is Neri's single built-in, immutable UTF-8 string storage type. The
`text` library exposes checked byte and scalar operations over that storage; it
does not introduce a `Text` wrapper or another string representation.

`byteLength(value)` reports the UTF-8 byte length. `byteAt(value, offset)`
returns a byte or `null` when the offset is outside the string. Byte offsets
are useful for interchange formats and native protocols, while scalar offsets
are useful for Unicode-aware processing.

`isScalarBoundary(value, offset)` accepts offsets from zero through the byte
length. `sliceBytes(value, start, length)` returns `null` for a negative or
out-of-range range, or when either endpoint splits a UTF-8 scalar. An empty
slice at the end is valid. `bytes(value)` copies the UTF-8 bytes into a new
`Byte[]`; changing that array cannot change the source string.

`copyBytes(value, target, offset)` copies directly into `target` and returns
`false` when the nonnegative offset and complete byte range do not fit. It does
not allocate an intermediate array. `fromBytes(value)` returns a new `String`
only for valid UTF-8 and otherwise returns `null`. `concat` and `equal` use the
built-in `String` operations.

`scalarCount(value)` counts Unicode scalar values. `scalarAt(value, index)`
returns the scalar value as an `Int`, or `null` for a negative or out-of-range
index. A scalar is not a grapheme cluster: for example, a base letter followed
by a combining mark has two scalars. The Unicode Standard defines a scalar as a
code point other than a surrogate and defines UTF-8 as one to four bytes per
scalar. [Unicode Standard, Chapter 3](https://www.unicode.org/versions/Unicode17.0.0/core-spec/chapter-3/)
specifies those ranges and encoding forms.

`Scalar` is a normal immutable library class, not string storage. Create one
with `Scalar.fromInt(code)`, which accepts U+0000 through U+10FFFF except
U+D800 through U+DFFF. Its `value()` conversion is explicit (`scalar as Int`).
`utf8()` creates its one-to-four-byte encoding, and `toString()` validates that
encoding through `fromBytes`, returning `String?`. `Scalar` equality compares
the scalar values.

`byteLength`, `byteAt`, and `isScalarBoundary` are constant time.
`Scalar.fromInt` has constant work and creates one `Scalar`; `utf8()` has
constant byte work and creates one array of one to four bytes. `sliceBytes`,
`copyBytes`, `bytes`, and `fromBytes` are linear in byte length. `scalarCount`
and `scalarAt` scan UTF-8 and are linear in the traversed byte length. `concat`
allocates the combined string and `equal` may compare each byte. These choices
follow the usual distinction between bytes, scalar values, and character
boundaries described by Rust's [String documentation](https://doc.rust-lang.org/std/string/struct.String.html#utf-8).
