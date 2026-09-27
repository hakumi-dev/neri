# Shared test catalog

`tests/catalog.json` names the compiler tests that should be compared across
macOS ARM64, Linux x86-64 and Windows x86-64. Every case has a stable ID,
applicable targets, required targets and capabilities. Language cases declare
their source files, compiler command, expected exit and output checks. Unit
cases name a project test unit; the runner obtains its arguments from the
project manifest and builds selected units in one batch. External cases name
an adapter for workflows that need setup or multiple commands.

The `test-runner` unit in `manifest.json` reads this catalog. Its command
line requires `--target`, `--root`, `--compiler`, `--native`, `--work`
and `--catalog`. Use `--release` for release-mode language compilation and
release requirements. Repeat `--capability NAME` for capabilities available
on that host. Register an external adapter with `--adapter NAME=PATH`. The
runner invokes an adapter with `--id ID --root ROOT --compiler COMPILER
--native NATIVE --work WORK --target TARGET`, followed by that case's catalog
arguments. It passes arguments directly to the child process and inherits the
runner's environment.

For a focused run, add `--filter ID-PART`. The filter is a substring of the
stable case ID. The result inventory still includes every catalog ID:
unselected cases are `filtered`, inapplicable targets and missing
capabilities or adapters are `unsupported` with a reason, executed cases
are `passed` or `failed`. A release run fails if a required case is
unsupported or if any case is filtered. Ordinary failures always fail the
run. The summary distinguishes selected, executed, unsupported and filtered
counts; `complete` is false when cases are filtered or a required capability
is unavailable.

The runner writes `results.jsonl` and `summary.json` under `--work`.
Each result includes its ID, status, reason, exit code and duration. The
summary includes target, counts, total duration and a SHA-256 digest of the
canonical catalog JSON, so line-ending differences do not change catalog
identity. Executed cases also write individual stdout and stderr files.

The shared runner is available as a project unit. The existing platform build
entry points still run their current validation paths. External catalog cases
without registered adapters are reported as unsupported; their presence in
the catalog does not imply that the shared runner has executed them.
