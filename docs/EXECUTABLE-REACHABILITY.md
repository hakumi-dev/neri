# Executable reachability

`use` makes names available for resolution. It does not initialize a module or
make every declaration in that module an executable root. Calls through qualified
names work without an import and retain the same implementation as unqualified
calls. Declared project dependencies still participate in checking and compilation.

Executable linking starts from the application entry point and explicit
`@cabiExport` functions. It follows implementation references, including function
addresses and virtual tables of allocated classes. A declaration that is neither
a root nor reachable from one can be removed from the executable, even when its
source belongs to a used namespace or a precompiled dependency object.

The compiler records implementation edges in versioned dependency declarations.
These edges select native libraries before invoking the linker, so an unreachable
native import does not require its library to be installed. Native section
collection removes unused implementation code and data from the final executable:
Mach-O uses dead stripping, ELF uses section garbage collection and as-needed
shared libraries, and COFF uses reference elimination. ELF and COFF compilation
places functions and data in separate sections, including the static runtime.

Interactive hosts preserve the runtime APIs needed by future session code. The
separate scalar `.nref` format uses its provider-wide runtime requirement summary;
its closed implementation contract excludes external native library imports.
Live virtual tables preserve their methods; this is
not an optimization that removes individual unused virtual slots. Explicit C
exports remain roots even when Neri code does not call them.
Native library selection conservatively includes possible virtual call targets;
the native linker can discard additional implementations that have no live
relocations.

Only the current dependency declaration schema is accepted. An incompatible local
cache image is rebuilt from source; it is not decoded through a legacy reader.
Missing implementation reachability metadata is an error. Reference artifacts
also require an exact toolchain identity and must be regenerated after changes.

This contract concerns executable contents and native dependencies. Compiler
memory use, dependency analysis, generic specialization, and the managed runtime's
baseline memory remain separate costs. A dependency used transitively is still
used. An imported namespace alone does not imply a database connection or another
resource allocation.

## Validation scope

The macOS arm64 contract inspects executable symbols and exercises unused
imports, qualified calls without `use`, callbacks, virtual dispatch, generics,
explicit C exports, and an unreachable import of a missing native library. It
checks debug and release builds, dependency cache reuse, and source compilation
with caching disabled. Separate reference and incremental build contracts cover
the existing artifact paths. Native IR boundary tests remain enabled.

Linux object emission has been inspected for separate function sections. Full
Linux and Windows executable runs require their platform validation jobs; a
local macOS run does not certify those targets or the full release gate.

With the local Sumi demo unchanged, its default executable decreased from
3,225,208 to 2,541,400 bytes (21.2%) on macOS arm64. Its `__TEXT` segment decreased
from 1,900,544 to 1,540,096 bytes. These are artifact measurements, not resident
memory measurements. The installed Sumi CRUD contract and console queries passed
against temporary SQLite databases. The HTTP server was not started by the audit.

## References

- [C# namespace specification](https://learn.microsoft.com/en-us/dotnet/csharp/language-reference/language-specification/namespaces)
  defines namespace imports as name-resolution directives.
- [.NET trimming](https://learn.microsoft.com/en-us/dotnet/core/deploying/trimming/trim-self-contained)
  documents removal of unused code as a separate deployment operation.
- [Dean, Grove and Chambers, ECOOP 1995](https://research.google/pubs/optimization-of-object-oriented-programs-using-static-class-hierarchy-analysis/)
  provides the class-hierarchy analysis context for preserving possible virtual
  call targets. Neri currently retains complete live virtual tables.
- [LLVM TargetOptions](https://llvm.org/docs/doxygen/classllvm_1_1TargetOptions.html),
  [GNU ld options](https://sourceware.org/binutils/docs/ld/Options.html), and
  [MSVC linker optimizations](https://learn.microsoft.com/en-us/cpp/build/reference/opt-optimizations?view=msvc-170)
  specify native section generation and collection. Apple's installed `ld(1)`
  manual defines `-dead_strip` and `-dead_strip_dylibs` for Mach-O.
