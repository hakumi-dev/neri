# Interactive terminals

`use terminal` loads the terminal library; `use clock` loads monotonic time.
Applications require no C files or manual native link step.

```ruby
use terminal
use console

def main(): Void
  let session = terminal.open()
  if session == null
    console.println("A foreground interactive terminal is required.")
    return
  end
  var running = true
  while running
    let key = session.readKey(100)
    if key != null
      running = false if key == "q" || key == "closed"
    end
  end
  session.close()
end
```

One session owns terminal input at a time. Opening requires foreground terminal
stdin and terminal stdout. Opening disables canonical input, echo and software
flow control, switches to the alternate screen and hides the cursor. Failure or
an existing session returns `null` without taking over its terminal.

`Session.readKey(timeout: Int): String?` waits up to 0–60000 milliseconds for the
first byte. `null` means timeout. Printable ASCII characters return themselves;
arrow keys return `up`, `down`, `left`, `right`. Space, Enter, Backspace and Escape
return `space`, `enter`, `backspace`, `escape`. Escape sequences allow two further
20-millisecond reads. Other bytes return `unknown`; this is not a Unicode text
input or complete terminal-protocol API. Unknown escape sequences are discarded.

EOF, I/O failure, invalid timeout, or an interruption returns `closed` and closes
the session. SIGINT, SIGTERM, SIGHUP, SIGQUIT and SIGTSTP request cooperative
closure on the next read; SIGTSTP closes this session rather than suspending it.
`Session.isOpen()` reports the session state. Signal handlers are restored on
close. Applications should keep reading while interactive and avoid mixing
`console.read()` with an active session.

`Session.columns()` and `rows()` query current dimensions, returning zero when
unavailable. `Session.close()` restores terminal settings, cursor and screen;
repeated closes are harmless, and a closed session cannot close a newer lease.
Close explicitly when leaving the interactive scope. Normal process exit and
Neri runtime panic also restore an active terminal. SIGKILL, native crashes and
loss of the terminal cannot guarantee restoration; `stty sane` restores a shell
left in a noncanonical mode. Garbage collection is not a session-close mechanism.

`terminal.escape(): String` supplies the ASCII Escape character for applications
that render ANSI control sequences through `console.print`.
`clock.Duration` represents a nonnegative millisecond duration. Static factories
`fromMilliseconds` and `fromSeconds` return null for negative values or overflow;
`milliseconds()` explicitly extracts the unit and `plus` checks addition.
`clock.Instant.now()` reads the monotonic clock as an optional instant.
`fromMonotonicMilliseconds` constructs a nonnegative reading for the same clock
domain, including deterministic tests. `later.since(earlier)` returns an optional
duration, `instant.plus(duration)` returns an optional deadline, and
`now.reached(deadline)` includes equality. Reversed time differences and addition
overflow return null. These types do not represent calendar dates or wall-clock
timestamps; callers compare readings from the same monotonic clock domain.
Consecutive readings may be equal, as specified by the
[clock_gettime contract](https://man7.org/linux/man-pages/man2/clock_gettime.2.html).

`new clock.Clock(read)` accepts a `fn(): clock.Instant?` time source;
`now()` invokes it once and preserves an unavailable (`null`) reading.
`clock.Clock.monotonic()` supplies the system monotonic source. Pass a clock to
code that evaluates elapsed time or deadlines. A custom source supplies
nondecreasing readings in one clock domain; the wrapper does not enforce this
condition or synchronize callback state.

`new clock.FakeClock(start)` owns an explicit monotonic reading. Its `clock()`
returns a source sharing that state, and `advance(duration)` moves time forward.
Overflow returns false and preserves the previous reading. Fake clocks and their
sources are confined to one execution thread. They allow deterministic deadline
tests without sleeping or changing the process clock. Clock injection follows
the testing principle described by [Java's Clock contract](https://docs.oracle.com/en/java/javase/25/docs/api/java.base/java/time/Clock.html);
Neri's contract here concerns monotonic time, with the units and concurrency
rules specified above.

`clock.milliseconds(): Int?` returns milliseconds from an arbitrary monotonic epoch,
or `null` if unavailable. It measures elapsed time, not civil time or timestamps.

Compiled executables statically include the Neri runtime. They need the target
operating system's libraries, but neither Neri nor LLVM at execution time.
