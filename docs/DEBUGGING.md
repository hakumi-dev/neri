# Debugging Neri

Neri executables use LLVM debug information and LLDB. An IDE connects to
`lldb-dap` through the Debug Adapter Protocol; the language server supplies
editing features independently of the debugger.

## Build and terminal

Build an executable in the default Debug configuration:

```sh
neri build main.hk --output ./app
lldb -- ./app
```

In LLDB, set a source breakpoint and start execution:

```text
breakpoint set --file main.hk --line 12
run
frame variable
next
step
finish
continue
```

`next` steps over a call, `step` enters it, and `finish` returns to its caller.
Use `bt` for the call stack and `frame select` to inspect another frame.
Arguments after the executable in `lldb -- ./app argument` reach the program.
`lldb -p <pid>` attaches to an existing process when the operating system permits it.

On macOS, a Debug executable has a companion `<executable>.dSYM` bundle. Keep
the executable and this bundle together when moving build artifacts. The
compiler runs `dsymutil` before deleting the temporary object; a symbol-linking
failure fails the build and preserves that object for diagnosis.
`NERI_DSYMUTIL` selects an alternative symbol-linking executable.

Debug executable builds record source file paths for breakpoints and navigation.
The IR keeps logical source identities; standalone IR, assembly and object
emission keep their logical debug paths. LLDB's `target.source-map` setting or
the adapter's `sourceMap` configuration maps paths when sources move.
`--release` selects optimized output without Neri source debug information.

## IDE connection

Configure an IDE with DAP support to launch `lldb-dap`, then supply an absolute
program path and its working directory. For example, a launch configuration
for the LLVM LLDB DAP extension uses:

```json
{
  "type": "lldb-dap",
  "request": "launch",
  "name": "Neri",
  "program": "${workspaceFolder}/app",
  "cwd": "${workspaceFolder}",
  "args": []
}
```

On macOS, `xcrun -f lldb-dap` locates the adapter supplied by Xcode. Each IDE
provides its own run configuration and breakpoint interface. Language-file
support alone does not register a debugger.

The Neri Rider plugin uses Rider 2026.2's DAP integration. Select **Debug** on a
Neri **Run** configuration: it builds an unoptimized executable and starts LLDB.
The configuration's optional **LLDB DAP** path selects the adapter. Builds run
off the UI thread, have a bounded lifetime, and terminate their process tree
when the session is cancelled.

## Inspection contract

Scalar variables, class fields, native records, fixed arrays and inline
optionals have concrete debug types. Managed classes appear as pointers;
`frame variable -P 1 value` expands their fields. Use LLDB's pointer/member
syntax in its expression console. Lexical scopes distinguish shadowed names,
and assignments and control-flow joins update the same debugger variable.

Managed arrays expose `length`, and strings expose `byte_length`. Automatic
expansion of their dynamic contents is unavailable in the current LLDB
integration. Alternative values expose their lowered class storage.

LLDB's command console accepts LLDB commands. Variable inspection uses the
debug information emitted by Neri. General expression evaluation follows
LLDB's evaluator and is not a paused Neri interpreter: Neri statements,
constructors, callbacks and arbitrary Neri method calls are not a supported
console contract. Adapter capabilities are negotiated with the installed
LLDB version.

## Verification and references

Run `scripts/build.sh debugger-test` on macOS with LLDB installed and local
debugging permitted. This opt-in Neri contract builds a fresh executable and
checks source breakpoints, stepping, variable updates and class fields through
a real LLDB process. The ordinary test suite does not require debugger access.

The integration follows these primary specifications and implementation guides:

- [LLVM source-level debugging](https://llvm.org/docs/SourceLevelDebugging.html)
  defines source locations, variable metadata and their relationship to optimized code.
- [LLVM dsymutil](https://llvm.org/docs/CommandGuide/dsymutil.html) and
  [LLDB symbols on macOS](https://lldb.llvm.org/use/symbols.html) define symbol linking
  and discovery for Mach-O executables.
- [Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/overview)
  defines the editor/debugger boundary.
- [LLDB DAP integration](https://lldb.llvm.org/use/lldbdap.html) documents adapter
  discovery, launch configuration and capability negotiation.
- [LLDB command map](https://lldb.llvm.org/use/map.html) documents source breakpoints,
  stepping, variables and source-path mapping.
