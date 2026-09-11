# Binary file reads

`files.ScopedRoot.open(absolutePath)` acquires a root through
`result.Result<ScopedRoot, result.Failure>`. Use it with `using` inside a callable
returning `resources.Outcome<T, result.Failure>` to release the root on scope
exit, early return, or failure acquiring a later resource. The scoped root exposes
bounded reads and directory enumeration while retaining its private handle.
Its `close` is idempotent; reads and enumeration after closure report `closed`.


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

## Rooted reads

`files.Root.open(absolutePath)` pins an existing directory and returns a root or
an owned failure. The configured root path is UTF-8, NUL-free, at most 1 MiB and
absolute, with no trailing separator except for `/` itself. Its final component must not be a symbolic link. Parent components may
resolve symbolic links intentionally as part of choosing the configured root;
the opened directory object, rather than its original spelling, becomes the
authorization anchor.

`root.readBytes(relativePath, limit)` reads a regular file through that pinned
directory. Paths use `/` separators and reject absolute paths, empty components,
`.` and `..`, `\`, `:`, NUL and trailing separators. The complete lexical path
is validated before any filesystem operation, so an invalid suffix is never
masked by an earlier missing component. Neri does not percent-decode the
path. Every component is opened relative to the preceding directory descriptor
without following symbolic links. This follows the descriptor-relative and
no-follow behavior of [POSIX `openat`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/openat.html),
then checks the opened object with
[POSIX `fstat`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fstat.html).

An opened directory object stays authorized if it is subsequently renamed.
Rooted reads do not provide a global filesystem snapshot or current-path
confinement against hostile renames of already opened ancestor directories.
Opening each later name remains relative to the retained authorized object, and
no-link traversal plus rejection of `..` prevents that name traversal from
escaping it. Hard links are allowed; this is a namespace containment contract,
not a file-origin contract.

The final size is checked before whole-file allocation. A concurrent size change
returns `changed_size`; concurrent same-size writes do not have snapshot
semantics. The byte and path limits match `readBytes`.

Root opening reports `invalid_root`, `root_open_failed`, `root_not_directory`,
`symlink_disallowed` or `unavailable`. POSIX may report `wrong_type` for a
no-follow directory open of a symbolic link; callers must treat both outcomes as
denial and neither outcome exposes target bytes. Reads additionally report `closed`,
`invalid_path`, `component_missing`, `component_denied`, `wrong_type` and the
existing metadata, limit, read and changed-size failures. Failures retain the OS
code, operation and zero-based component index when available. Temporary child
descriptors close on every supported return. The primary operation failure and
a secondary `closeFailure` remain separate. Additional cleanup failures form a
`closeFailure` chain in the order the closes were attempted.

Directory opens require directory descriptors at the operating-system boundary.
Neri checks the final descriptor's regular-file metadata before reading and owns
its close, preserving a secondary `closeFailure` on metadata and read failures.
`bytesRead` records consumed bytes, including the extra size-change probe byte.

`root.close()` is idempotent. A failed close reports `close_failed`; the root is
still considered closed because retrying could close a reused descriptor. This
ownership contract releases the descriptor on an explicit close.

Rooted access is currently available on POSIX targets. Windows returns
`unavailable`; matching the race contract requires verified component-relative
handle traversal rather than string-prefix canonicalization. Windows
[`CreateFileW`](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew)
and [`GetFinalPathNameByHandleW`](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfinalpathnamebyhandlew)
document the relevant handle and resolved-path behavior, but resolved-path checks
alone are not the containment primitive promised here.
