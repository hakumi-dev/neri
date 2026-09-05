# Start with examples

With Neri installed and on your `PATH`, run these commands from the repository
root or the extracted package directory:

```sh
neri examples/hello.hk
# Hello, Neri!

neri examples/functions.hk
# Total: 60

neri run examples/arguments.hk -- Ada "Grace Hopper"
# Hello, Ada!
# Hello, Grace Hopper!
```

Read them in that order: an entry point and output, typed functions and arrays,
then command-line arguments. `let` binds a value; `var` allows reassignment.
`host.argumentAt` returns an optional string, so check for `null` before using it.
Arguments after `--` belong to the program; running the arguments example without
arguments produces no output.

Build a standalone executable in the current directory:

```sh
neri build examples/arguments.hk --release
./arguments Ada
# Hello, Ada!
```

From a source checkout, `scripts/neri.sh` can replace `neri` after
`scripts/build.sh test`. See the [language reference](../docs/LANGUAGE.md) for the
current syntax, types, and tooling contracts.
