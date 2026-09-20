# Rooted change snapshots

`files::Root.list(relative, limit)` enumerates one directory below an open root.
The root and every intermediate directory remain private descriptors. Relative
paths receive the same complete lexical validation as rooted reads. Entries are
reported as `file`, `directory`, `symlink`, or `other`; enumeration never follows
an entry. The default and maximum result limit is 4096. Open, iteration, and
close failures retain their operation and platform code.

`changes::Scanner.poll(root, relative)` recursively builds a deterministic SHA-256
snapshot. Neri sorts entries, records empty directories, hashes exact file bytes,
and enforces 4096 entries, 128 MiB total file content, and depth 64 by default.
Symlinks and other file types fail with `unsupported_entry`. POSIX rooted
enumeration is supported; Windows reports `unavailable`.

The scanner commits its private baseline only after every enumeration, read,
close, limit, and hash operation succeeds. A failed candidate leaves the prior
baseline intact. Returned digest bytes are independent copies, so caller changes
cannot mutate the baseline. Because the supplied `Root` owns its descriptor, a
rename of the root path does not redirect later polls.

Each poll reads live directory entries and file contents sequentially. Concurrent
filesystem mutations can produce a mixed observation or a failure; the result
does not provide an atomic filesystem snapshot. Stable trees produce the same
digest independently of directory enumeration order.
