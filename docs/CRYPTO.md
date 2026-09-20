# Digests, entropy, and hexadecimal encoding

`use crypto` provides the cryptographic operations required for content
digests and restart-scoped identifiers. It delegates cryptographic primitives
to maintained operating-system or platform libraries.

## Results and failures

Operations that can fail return `crypto::BytesResult`. Exactly one branch is
selected: `bytes()` contains owned output on success, while `failure()` contains
a `crypto::Failure` with a stable `code()` on failure. Successful byte arrays
belong to the caller and remain mutable.

## SHA-256

`crypto::sha256(input)` hashes the exact bytes in `input`, including embedded
zero bytes, and returns a 32-byte digest. It uses CommonCrypto on Apple
platforms, OpenSSL's high-level EVP digest API on Linux, and CNG BCrypt on
Windows. A native provider or input-size failure has code `digest_failed`.
Linux executables that use this module require the build host's compatible
OpenSSL `libcrypto` shared library on the execution host; compilation needs
the OpenSSL development package (`libssl-dev` on the supported Ubuntu setup).

The implementation uses maintained providers rather than implementing SHA-256
in Neri. OpenSSL recommends its
[high-level EVP digest interface](https://docs.openssl.org/3.3/man3/EVP_DigestInit/),
and the digest contract is checked against NIST's published
[SHA-256 `abc` example](https://csrc.nist.gov/CSRC/media/Projects/Cryptographic-Standards-and-Guidelines/documents/examples/SHA256.pdf).

## Operating-system entropy

`crypto::randomBytes(count)` obtains `count` bytes from the operating system.
`count` must be between zero and 256 inclusive; invalid sizes have code
`invalid_length`, and an operating-system failure has code `entropy_failed`.
The limit matches the small-request guarantee of Linux
[`getrandom`](https://man7.org/linux/man-pages/man2/getrandom.2.html) and is
ample for restart identifiers.

Apple builds use `getentropy`, Linux builds use `getrandom`, and Windows builds
use the system-preferred generator through
[`BCryptGenRandom`](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptgenrandom).

`EntropySource` wraps an injectable `fn(Int): BytesResult` source.
`EntropySource.system()` supplies the operating-system implementation. The
`bytes(count)` method enforces the same bound before invoking a source and
rejects a successful response of the wrong length with `invalid_source`. This
supports deterministic and failure-path tests without shared mutable state.

## Hexadecimal encoding

`crypto::encodeHex(bytes)` produces lower-case hexadecimal. It returns `null`
when doubling the input would exceed the 128 MiB buffer limit.

`crypto::decodeHex(text, limit = 4096)` accepts upper- or lower-case ASCII
hexadecimal and returns bytes. `limit` bounds the decoded byte count before
allocation and may be at most 128 MiB. Odd-length input, non-hexadecimal bytes,
and non-ASCII text have code `invalid_hex`; excess input has code
`limit_exceeded`; an invalid limit has code `invalid_limit`.

These APIs do not provide HMAC, password hashing, authentication, TLS, key
storage, or general-purpose encoding formats.
