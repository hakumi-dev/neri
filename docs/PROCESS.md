# Supervised processes

`use process` starts and owns child processes without invoking a shell.
`SpawnOptions` keeps the executable separate from its arguments, so spaces,
Unicode, quotes, and shell metacharacters remain literal argument data.

`process.ScopedChild.spawn(options)` returns
`result.Result<ScopedChild, result.Failure>`. Acquiring it with `using` cancels
and reaps the child when the scope exits, including early return and a failed
later acquisition. Its `close` is idempotent and forwards native cleanup errors
to the enclosing `resources.Outcome`. `poll` and `wait` after closure report
`disposed`. The process token and underlying `Child` remain private.

`process.spawn(options)` returns a `SpawnResult`. A successful result contains a
`Child`; failure contains a stable `Failure.code()`, its `operation`, and the
exact platform `osCode` when the operating system supplied one. `spawnOutcome`,
`pollOutcome`, `waitOutcome`, `cancelOutcome`, and `disposeOutcome` expose the
same ownership model through `result.Result` and `result.Failure`. Arguments and environment
are encoded with explicit byte lengths. Embedded NUL is rejected. A configuration
may contain at most 1024 total arguments, 1024 environment overrides, and 1 MiB
of encoded configuration. Environment names are nonempty, contain no `=`, and
are unique under ASCII case folding. Overrides replace inherited values;
`inheritEnvironment = false` starts from an empty environment.

The working directory is optional. Standard input is EOF. Standard output and
standard error are captured separately as exact bytes, including invalid UTF-8
and NUL. Each capture limit is fixed before launch and may be at most 128 MiB.
Native reader threads continue draining after a limit is reached, discard the
excess, and set the corresponding truncation flag. Output volume therefore does
not require the application to poll promptly and cannot block a child solely
because the retained prefix is full.

`Child.poll()` returns immediately. `wait(milliseconds)` waits for at most
0–60000 milliseconds and returns the same snapshot shape. A running result has
no exit status. A completed result reports `exit`, `signal`, or `cancelled` and
the platform status value. Every snapshot owns its byte arrays.

`cancel()` forcibly terminates the owned execution domain and waits for the
direct child. `dispose()` performs cancellation when needed, joins capture
threads, releases native resources, and is idempotent. Callers dispose every
child explicitly. Runtime shutdown must apply the same cleanup to abandoned
native tokens. Capture readers stop after a bounded final drain when the owned
domain completes. A descendant that escapes the domain while retaining a pipe
cannot keep `poll`, `cancel`, or `dispose` blocked; unread bytes are discarded
and the channel is reported as truncated.

On POSIX systems each child starts in a new process group. Cancellation signals
that group and reaps the direct child. This owns the process group; descendants
that deliberately move into another process group or session are outside the
termination guarantee. Launch uses an error pipe closed by successful `execve`,
which distinguishes an actual program exit status 127 from a failed executable
replacement. This is required because
[`posix_spawn` may report launch success and make the child exit 127](https://man7.org/linux/man-pages/man3/posix_spawn.3.html#RETURN_VALUE).

Windows starts the process suspended, assigns it to a job configured with
`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`, and resumes it only after ownership is
established. An explicit inherited-handle list contains only EOF stdin and the
two output pipe writers. The executable is passed separately from the quoted
mutable command line, avoiding the ambiguous spaced-path behavior documented
for [`CreateProcessW`](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw).
Microsoft documents both
[restricted handle inheritance](https://learn.microsoft.com/en-us/windows/win32/procthread/inheritance)
and [job-object tree cleanup](https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects).

Candidate replacement is ordinary Neri orchestration: build and spawn the
candidate first, leave the current child untouched when that operation fails,
then cancel and dispose the previous child only after candidate success.
Readiness checks, debounce, rebuild selection, and application reload policy
belong to the consumer.
