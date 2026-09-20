# Windows

Neri supports native Windows x86-64 builds using the Win32 platform layer and
the MSVC ABI. WSL and MinGW are outside this setup.

## Requirements

Use a native x64 Windows terminal with PowerShell 7, Git and:

- Visual Studio's **Desktop development with C++** workload, including the x64
  MSVC tools and a Windows SDK.
- Node.js 22 or newer for the Windows language and LSP checks.
- CMake 3.28 or newer and Ninja 1.11 or newer. The Visual Studio installation
  may provide these; `scripts/windows-env.ps1` adds its copies to `PATH`.

LLVM 22.1.8 is downloaded from the LLVM GitHub release on first use. The
archive is about 862 MB and its SHA-256 is pinned in
`bootstrap/windows-x86_64.json`. The compiler IR seed is included in the
repository. PowerShell verifies its SHA-256 digests and decompresses it through
the .NET gzip API before the native backend materializes the host compiler.

## Build, test and install

From the repository root, run:

```powershell
pwsh -File scripts/build.ps1 install
```

The command configures and builds the native components, bootstraps three
compiler generations, and checks byte-identical Neri IR, COFF and PE output.
It then runs the Windows contract suite: 74 CLI cases, UTF-8 paths, and LSP
framing and diagnostics. The native CTest suite runs as part of the build.

The default installation is `%USERPROFILE%\.neri`. The launcher is installed at
`%USERPROFILE%\.neri\bin\neri.exe`; each validated toolchain is kept under
`toolchains\<artifact-manifest-sha256>`, and `toolchain.txt` selects the active
toolchain. The installer adds the bin directory to the per-user `PATH`. Open a
new terminal, and restart Rider, after installation. Use `-Prefix` to select a
different installation directory or `-NoPath` to leave `PATH` unchanged.

An install copies the runtime LLVM subset it needs into
`<prefix>\dependencies\llvm-22.1.8-<clang-sha256>`: `clang++.exe`, `lld-link.exe`
and the `lib\clang` resources. This keeps the installed compiler independent of
the checkout's `build\tools` directory. The full LLVM development tree remains
a build dependency. Visual Studio's MSVC tools and Windows SDK remain external
runtime-linking dependencies, and their `LIB`/`INCLUDE` environment is captured
when the toolchain is installed.

Activation is performed only after the candidate toolchain passes its hashes and
smoke test. If activation or that final check fails, the previous launcher and
`toolchain.txt` selection are restored.

For native CMake development, load the Visual Studio and LLVM environment first:

```powershell
. .\scripts\windows-env.ps1
cmake --preset windows-debug
cmake --build --preset windows-debug --parallel 4
ctest --preset windows-debug

cmake --preset windows-release
cmake --build --preset windows-release --parallel 4
ctest --preset windows-release
```

The presets build the C/C++ code generator and runtime. They do not run the
self-hosted compiler bootstrap; use `scripts/build.ps1` for that workflow.

## Scope and current status

The Windows runtime uses Win32 sockets, console handling, UTF-8/UTF-16 path
conversion, and the MSVC-compatible ABI. POSIX-only suite helpers such as the
worker and PTY helpers have no direct Windows port with identical behavior.
Windows-specific tests and helper coverage are still being expanded.

The local Windows validation recorded three identical compiler generations and
7/7 CTest cases in both Debug and Release, plus the 74-case CLI, UTF-8 path,
and LSP checks above. The HTTP example contracts were also verified locally.
The repository workflow retains the macOS and Linux
jobs and adds Windows Debug/Release jobs plus a `Required / supported platforms`
gate. Branch protection for `main` requires that gate, so all supported-platform
jobs are enforced on remote pull requests.
