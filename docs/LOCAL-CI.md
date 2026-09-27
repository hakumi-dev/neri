# Local CI

`scripts/local-ci.sh` runs the supported-platform CI matrix from an Apple Silicon
Mac. The orchestration is written in Neri. Small host adapters control Tart for
macOS ARM64, Lima/QEMU for Ubuntu x86-64, and QEMU for Windows Server x86-64.
Linux and Windows use CPU emulation on this host; their durations are not
comparable to native GitHub runners.

`tooling/ci/matrix.json` defines the ten jobs consumed by both the local runner
and `.github/workflows/ci.yml`. `scripts/ci/jobs.sh` and `scripts/ci/jobs.ps1`
contain the shared guest preparation and validation commands, including the
LLVM version check and SQLite source checksum. The images match the required
OS family and architecture. They are not copies of GitHub's hosted images.

| Command | Contract |
| --- | --- |
| `./scripts/local-ci.sh plan` | Show the selected jobs without provisioning. |
| `./scripts/local-ci.sh doctor` | Check provider prerequisites; missing inputs produce a nonzero result. |
| `./scripts/local-ci.sh snapshot` | Archive the current tracked and nonignored untracked sources, including local modifications. |
| `./scripts/local-ci.sh provision` | Prepare managed guests from the configured inputs. |
| `./scripts/local-ci.sh run` | Provision, start, prepare, validate and clean up managed guests sequentially. |

Use `--platform macos`, `--platform linux`, `--platform windows`, or `--job ID`
to select a subset. A subset is not a complete supported-platform validation.

Local state, requests, source archives and command output belong under
`build/local-ci/`. The local configuration is `build/local-ci/config.json`;
the checked-in example contains public image pins and empty fields for private
machine paths. Windows requires an official Server 2025 x64 ISO, its verified
SHA-256, the selected installation image, and the declared offline tool inputs.
Missing media is a prerequisite failure, not a successful Windows test.

Copy `tooling/ci/local-ci.json` to that local configuration path to supply
machine-specific inputs. File pins use SHA-256. The Visual Studio offline
layout pin is produced by
`tooling/ci/providers/windows-qemu-tcg.sh fingerprint-layout /absolute/layout`.
The helper rejects symbolic links and hashes sorted, NUL-framed relative
filenames and file digests. Image selection is explicit: `imageIndex` must
match the desired edition in the supplied Windows ISO.

The runner checks the source archive hash and sends the same snapshot to each
guest. It does not mount the host checkout into the guest. Source snapshot
creation rejects symbolic links and detects changes while packaging. Only
instances recorded by the runner are eligible for lifecycle operations.
Jobs reuse one VM per platform, with a separate source checkout for each job.
Provisioning and boot have their own configured time limits; the matrix
deadline begins when guest preparation and validation start.

Job success requires the guest preparation command, validation command and
cleanup to succeed. Provider availability, VM creation and guest startup are
reported separately from test success. Image and tool prerequisites must be
present before a three-platform run can complete.

The provisioning contracts follow the primary documentation for
[Tart](https://tart.run/quick-start/),
[Lima](https://lima-vm.io/docs/config/),
[QEMU system emulation](https://www.qemu.org/docs/master/system/introduction.html),
[Windows Server 2025 media](https://www.microsoft.com/en-us/evalcenter/download-windows-server-2025)
and [GitHub-hosted runner platforms](https://docs.github.com/en/actions/reference/runners/github-hosted-runners).
