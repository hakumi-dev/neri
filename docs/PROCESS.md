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

The working directory is optional. `SpawnOptions.input` supplies an initial byte
payload to standard input. The runtime writes it concurrently with output
capture and closes the pipe after the last byte, so the child observes explicit
EOF. An empty payload provides immediate EOF. The payload shares the 1 MiB
encoded-configuration limit. If the child closes standard input early, the
runtime stops writing without delivering `SIGPIPE` to the parent process.

Standard output and standard error are captured separately as exact bytes,
including invalid UTF-8 and NUL. Each capture limit is fixed before launch and
may be at most 128 MiB.
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

`interrupt()` requests graceful shutdown without waiting. On POSIX it sends
`SIGINT` to the owned process group. On Windows it sends `CTRL_BREAK_EVENT` to
the child's process group; the call reports `interrupt_failed` when the child
does not share the caller's console. An interrupted child keeps its ordinary
`exit` or `signal` status. Only `cancel()` produces `cancelled`.

`process.run(options, timeoutMilliseconds)` covers bounded one-shot commands.
It starts the child, waits for completion, always disposes it, and returns an
owned output snapshot. A command still running at the deadline is cancelled
and returns a `timeout` failure from `process.run`.

On POSIX systems each child starts in a new process group with SIGINT unblocked
and its default disposition restored before executable replacement. Cancellation signals
that group and reaps the direct child. This owns the process group; descendants
that deliberately move into another process group or session are outside the
termination guarantee. Launch uses an error pipe closed by successful `execve`,
which distinguishes an actual program exit status 127 from a failed executable
replacement. This is required because
[`posix_spawn` may report launch success and make the child exit 127](https://man7.org/linux/man-pages/man3/posix_spawn.3.html#RETURN_VALUE).
The input writer uses nonblocking pipe writes and blocks `SIGPIPE` in its own
thread. macOS additionally applies `F_SETNOSIGPIPE` to the write descriptor as
documented by Apple for
[`fcntl`](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/fcntl.2.html).
This matches the POSIX definitions of
[`write`](https://pubs.opengroup.org/onlinepubs/9699919799/functions/write.html)
and process-group
[`kill`](https://pubs.opengroup.org/onlinepubs/009604499/functions/kill.html).

Windows starts the process suspended, assigns it to a job configured with
`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`, and resumes it only after ownership is
established. An explicit inherited-handle list contains only the stdin pipe
reader and the two output pipe writers. The executable is passed separately from the quoted
mutable command line, avoiding the ambiguous spaced-path behavior documented
for [`CreateProcessW`](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw).
Microsoft documents both
[restricted handle inheritance](https://learn.microsoft.com/en-us/windows/win32/procthread/inheritance)
and [job-object tree cleanup](https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects).
The input writer uses bounded chunks; cleanup cancels a pending synchronous
write as documented for
[`WriteFile`](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-writefile).
Graceful interruption follows the documented console-group rules for
[`GenerateConsoleCtrlEvent`](https://learn.microsoft.com/en-us/windows/console/generateconsolectrlevent).

Candidate replacement is ordinary Neri orchestration: build and spawn the
candidate first, leave the current child untouched when that operation fails,
then cancel and dispose the previous child only after candidate success.
Readiness checks, debounce, rebuild selection, and application reload policy
belong to the consumer.

## Terminal capture

`SpawnOptions.terminal = true` connects all three standard descriptors to a
new pseudoterminal on macOS and Linux. The child owns a new session and
controlling terminal, initially 80 columns by 24 rows. Its combined terminal
output is captured in `stdout`; `stderr` is empty. The `stdoutLimit` and
`stdoutTruncated` contracts apply to this combined stream. Terminal processing
is preserved, including the usual newline-to-CRLF output conversion.

Terminal capture accepts an empty `input` only. The terminal remains open while
the child runs; this mode provides output/terminal-detection tests, not an
interactive keyboard API or an EOF-producing pipe. `interrupt`, `cancel`,
bounded waiting and disposal retain their process ownership contracts. On
Windows terminal capture reports a spawn failure with `ERROR_NOT_SUPPORTED`.

The allocation follows
[`posix_openpt`](https://man7.org/linux/man-pages/man3/posix_openpt.3.html),
[`grantpt`](https://man7.org/linux/man-pages/man3/grantpt.3.html) and `unlockpt`.
The child establishes its session using
[`setsid`](https://man7.org/linux/man-pages/man2/setsid.2.html) and acquires the
slave as its controlling terminal using
[`TIOCSCTTY`](https://man7.org/linux/man-pages/man2/TIOCSCTTY.2const.html).

## CLI integration tests in Neri

Compile a test executable that receives the application executable as an
argument. `process.run` owns the command lifecycle; the test checks its exit
status and captured bytes after disposal:

```neri
use host
use process
use result
use test

def main(): Void
  let executable = host.argumentAt(0)

  test.assertTrue(executable != null)
  if executable == null
    return
  end
  let options = new process.SpawnOptions(executable)

  options.arguments = ["--help"]
  match process.run(options, 5000)
    case result.Result.Ok(completed)
      let status = completed.exit

      test.assertTrue(status != null)
      if status != null
        test.assertTrue(status.isExited())
        test.assertEqual(status.value(), 0)
      end
      test.assertTrue(!completed.stdoutTruncated)
    case result.Result.Error(failure)
      test.assertTrue(false)
  end
end
```

Use `files.TemporaryDirectory.create` with `using` for isolated fixtures and cleanup,
`files` operations for filesystem changes, and `httpclient` for bounded
loopback requests. The runtime-contract test manifests declare a `fixture`
and a `driver`; the build discovers those manifests and runs both debug and
release configurations. Application readiness and expected responses are
ordinary test code owned by the application.
