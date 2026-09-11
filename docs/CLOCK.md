# Clock and timestamps

`use clock` provides separate types for elapsed-time measurement and wall-clock
timestamps. The domains are explicit: `Instant` values are monotonic readings,
while `Timestamp` values are signed milliseconds from the Unix epoch.

## Monotonic time

`Duration` represents a nonnegative number of milliseconds. Its
`fromMilliseconds` and `fromSeconds` factories return `null` for negative values
or overflow. `milliseconds()` extracts the value and `plus` performs checked
addition.

`Instant.now()` reads the operating system's monotonic clock, corresponding to
the elapsed-time domain defined by [POSIX clocks](https://pubs.opengroup.org/onlinepubs/9699919799/functions/clock_getres.html).
`fromMonotonicMilliseconds` constructs a nonnegative reading in the same domain
for deterministic tests. `later.since(earlier)` returns a duration,
`instant.plus(duration)` returns a deadline, and `now.reached(deadline)` includes
equality. Reversed differences, arithmetic overflow, and unavailable system
readings return `null`. Compare instants only within the same clock domain;
consecutive system readings may be equal. This follows the POSIX distinction
between [`CLOCK_MONOTONIC` and `CLOCK_REALTIME`](https://pubs.opengroup.org/onlinepubs/9699919799/functions/clock_getres.html).

`Clock` wraps an injectable `fn(): Instant?` source. `Clock.monotonic()` supplies
the system source. `FakeClock` owns an explicit reading and advances it by a
duration. Failed advancement preserves its previous value.

## Wall-clock time

`Timestamp.fromUnixMilliseconds(value)` constructs a signed Unix timestamp.
Negative values represent times before 1970. `unixMilliseconds()` extracts the
value, and `plus(duration)` returns `null` on signed integer overflow.

`Timestamp.now()` and `WallClock.system().now()` read the operating system's UTC
wall clock and return `null` if the platform read or millisecond conversion
fails. Wall clocks can move backward or forward when the operating system time
is adjusted. Use monotonic instants for elapsed time and deadlines.

`formatUtc()` produces a deterministic, locale-independent timestamp with the
fixed shape `YYYY-MM-DDTHH:mm:ss.SSSZ`. Its civil-date conversion follows
[Howard Hinnant's Gregorian algorithm](https://howardhinnant.github.io/date_algorithms.html#civil_from_days).
It uses the proleptic Gregorian calendar,
UTC, and millisecond precision. Years `0000` through `9999` are representable;
timestamps outside that range return `null`. Formatting does not consult locale,
timezone, daylight-saving, or external calendar data. Parsing, civil calendar
types, timezone databases, and locale-specific presentation are outside this
contract. The fixed field order, four-digit year, `T` separator, fractional
seconds, and `Z` UTC marker follow the [RFC 3339 Internet timestamp profile](https://datatracker.ietf.org/doc/html/rfc3339#section-5.6).
Unix milliseconds do not encode leap seconds, so this formatter emits seconds
from `00` through `59`.

The Gregorian day conversion is the bounded integer form of Howard Hinnant's
[`civil_from_days` algorithm](https://howardhinnant.github.io/date_algorithms.html#civil_from_days).
On Windows, the platform adapter converts the UTC `FILETIME` epoch and
100-nanosecond unit documented for
[`GetSystemTimePreciseAsFileTime`](https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-getsystemtimepreciseasfiletime)
to signed Unix milliseconds.

`WallClock` wraps an injectable `fn(): Timestamp?` source. `FakeWallClock` owns a
timestamp, exposes a source through `wallClock()`, and advances by nonnegative
durations. Failed advancement preserves its previous value. Custom and fake
clock state is confined to one execution thread; the wrappers do not synchronize
callbacks.

The low-level `milliseconds()` and `wallMilliseconds()` functions expose the
system readings directly as optional integers. Prefer the typed APIs in
application code so monotonic readings and Unix timestamps cannot be confused.
