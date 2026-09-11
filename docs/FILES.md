# Binary file reads

`use files` loads bounded binary file reads. `files.readBytes(path, limit)`
returns a result with optional `bytes`, `failure` and `closeFailure`. Success
contains the exact bytes of a regular file, including invalid UTF-8, and closes
its descriptor before returning. The caller limit is 0–128 MiB; the file size is
checked before allocating one managed array. Indexed generation initializes the
array in linear time with one worker. Reads retry interruptions and handle
partial progress. A changed size is rejected; concurrent same-size writes do
not have snapshot semantics. Paths are UTF-8, at most 1 MiB, with no embedded NUL.

Failures expose a stable `code` and the captured native `osCode` when available:
`invalid_limit`, `invalid_path`, `open_failed`, `not_regular`, `metadata_failed`,
`limit_exceeded`, `read_failed` and `changed_size`. A separate `close_failed`
preserves the original failure when closing also fails. Any failure returns no
bytes. Descriptors are closed on supported returns; fatal process/runtime
failures retain the language's existing cleanup limits. The API follows symlinks
and provides no directory-containment contract. It requires runtime ABI 1.11 and
the files feature; platform behavior is verified separately per host. Native
read counts and EOF follow the [Darwin read contract](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/read.2.html)
and [Windows CRT read contract](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/read).
Windows opens with `_O_BINARY` to preserve bytes. Cleanup attempts one close
and reports its failure; it does not retry a potentially reused descriptor.
