# HTTP stop and drain policy

The sequential HTTP listener finishes the active request after a stop request,
closes the accepted socket and listener once, and returns a normal stopped
result. `Options.handleInterrupts` also turns `SIGINT` and `SIGTERM` into stop
requests while the listener owns the interrupt lease. An idle listener checks
that lease at most every 50 milliseconds.

`Options.forceExitAfterStopMilliseconds` is an explicit opt-in fatal boundary.
It accepts 1 through 60000 milliseconds. Once `Stop.request()` is called, or an
owned interrupt becomes pending, the active request may finish normally until
that duration expires. If it is still running, a native watchdog requests whole
process termination with `_Exit(124)`. The deadline therefore does not depend on
user handler cooperation.

Forced exit does not unwind the active handler and does not run user cleanup,
flush callbacks, or runtime shutdown. The operating system reclaims process
descriptors, though close work and scheduling mean the deadline is not a
real-time termination guarantee. Child processes are outside this policy; a
supervisor that owns a process group remains responsible for them. Linux
documents the process-wide `_Exit` behavior in
[`_exit(2)`](https://man7.org/linux/man-pages/man2/exit.2.html).

Without `forceExitAfterStopMilliseconds`, stop remains cooperative and existing
behavior is unchanged. A cooperative token alone cannot bound an arbitrary
handler that loops or blocks forever.
