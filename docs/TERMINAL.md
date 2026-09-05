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
`clock.milliseconds(): Int?` returns milliseconds from an arbitrary monotonic epoch,
or `null` if unavailable. It measures elapsed time, not civil time or timestamps.

Compiled executables statically include the Neri runtime. They need the target
operating system's libraries, but neither Neri nor LLVM at execution time.
