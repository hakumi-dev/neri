# Supported platforms

Neri's native target policy currently supports these triples:

| Target | Native ABI and development path |
| --- | --- |
| `macos-arm64` | Apple silicon macOS; bootstrap and package workflow in `scripts/build.sh` |
| `linux-x86_64` | Native Linux build with Clang/LLVM 22.1.8; source setup in [LINUX.md](LINUX.md) |
| `windows-x86_64` | Native Windows x64 with Win32 APIs and the MSVC ABI; setup in [WINDOWS.md](WINDOWS.md) |

WSL, MinGW and other Windows targets are outside the supported platform policy.
The POSIX worker and PTY test helpers are not assumed to work unchanged on
Windows; Windows-specific coverage remains in progress.

The CI workflow keeps the macOS and Linux jobs and includes Windows Debug and
Release jobs. Its `Required / supported platforms` job checks all three
platform results. Branch protection for `main` requires that check, enforcing
supported-platform coverage on remote pull requests.
