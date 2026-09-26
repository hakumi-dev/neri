# JSON

`use json` provides a bounded JSON tree codec. It has no compiler or reflection dependency.

```neri
use json
use result

match json::parse("{\"enabled\":true}")
  case result::Result.Ok(value)
    let enabled = value.get("enabled")
  case result::Result.Error(error)
    # error.code identifies the failure; error.offset is a byte offset.
end
```

`parse(String, Options? = null)` and `parseBytes(Byte[], Options? = null)` return `result::Result<json::Value, json::Failure>`. `stringify(Value, Options? = null)` returns `result::Result<String, json::Failure>`. The byte entry point validates UTF-8 before parsing. Each call returns a complete value or a failure; partial values and partial output are not exposed.

The grammar follows [RFC 8259](https://www.rfc-editor.org/rfc/rfc8259). All six JSON value kinds and top-level scalar values are supported. Numbers retain their exact lexemes, including exponent spelling and negative zero, without floating-point conversion. Strings decode escapes and valid UTF-16 surrogate pairs. Raw control characters, lone surrogates, invalid UTF-8, duplicate decoded object keys, byte-order marks, malformed numbers, and trailing content fail. Duplicate keys are compared after escape decoding, without Unicode normalization.

## Values

Factories are `Value.nullValue()`, `Value.boolean(Bool)`, `Value.string(String)`, `Value.number(String)`, `Value.array()`, and `Value.object()`. The number factory returns `Value?` and rejects text outside JSON's number grammar.

`kind()` returns `null`, `boolean`, `string`, `number`, `array`, or `object`. `text()` returns decoded strings, number lexemes, or `true`/`false` text for booleans; containers and null return Neri `null`. `count()` reports container entries. `get(key)` retrieves an object member and `at(index)` retrieves an array element; absent entries or wrong container kinds return Neri `null`.

`add(value)` appends to an array. `put(key, value)` appends an object member and rejects an existing key. Both return `Bool` and reject a wrong container kind or a container already holding 1,000,000 entries. `each(visit)` visits entries in insertion order with `(String?, Value)` arguments; array keys are Neri `null`. Each traversal observes the entry count at its start. Values are shared references, so mutating a child is visible through its parents.

Object lookup and duplicate detection use an [AVL tree](https://www.mathnet.ru/eng/dan26964), with logarithmic comparisons even for adversarial key insertion orders. Each comparison is bytewise and can inspect the full key. Appends and iteration use a separate linked sequence. Array indexing is linear; use `each` for a full traversal. String and output accumulation use geometrically growing bounded byte buffers. Serialization stops traversal on its first failure.

## Limits and failures

| Option | Default | Accepted range |
| --- | ---: | ---: |
| `maxInputBytes` | 1,048,576 | 0–134,217,728 |
| `maxOutputBytes` | 1,048,576 | 0–134,217,728 |
| `maxDepth` | 64 | 0–256 |
| `maxNodes` | 100,000 | 1–1,000,000 |

Every call validates all options and uses their values for that operation. Depth starts at zero for the root; each array element or object member value adds one level. Nodes include every JSON value, including containers, and exclude object keys. Input limits count all UTF-8 bytes, including whitespace. Output limits count escaped UTF-8 output bytes. Stringify also enforces depth and node limits, so cyclic value graphs fail within those limits.

`json::validOptions(options)` checks these ranges without parsing or allocating a
value tree. Framework adapters can validate caller options before applying a
smaller transport-specific limit to a copy.

Failure codes are `invalid_options`, `input_limit`, `output_limit`, `depth_limit`, `node_limit`, `invalid_utf8`, `invalid_json`, and `duplicate_key`. Parse errors report a zero-based input byte offset at detection. Invalid UTF-8 reports the offending byte, or the start of a truncated final sequence. The diagnostic scan runs only after byte decoding fails. Option/input-limit failures and stringify failures use offset zero. Stringify emits compact JSON in insertion order, escaping controls, quotation marks, and backslashes; other Unicode scalars remain UTF-8. It does not perform canonical sorting or number normalization.
