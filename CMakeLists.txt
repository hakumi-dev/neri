cmake_minimum_required(VERSION 3.28)

project(NeriNative LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE MATCHES "^(Debug|Release|RelWithDebInfo|MinSizeRel)$")
  message(FATAL_ERROR "Set CMAKE_BUILD_TYPE explicitly (for example Release or Debug).")
endif()

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" NERI_VERSION LIMIT_COUNT 1)
set(NERI_LLVM_VERSION "22.1.8" CACHE INTERNAL "Pinned LLVM version" FORCE)

if(NOT CMAKE_GENERATOR STREQUAL "Ninja")
  message(FATAL_ERROR "Neri native builds require the Ninja generator.")
endif()

execute_process(
  COMMAND "${CMAKE_MAKE_PROGRAM}" --version
  OUTPUT_VARIABLE NERI_NINJA_VERSION
  OUTPUT_STRIP_TRAILING_WHITESPACE
  COMMAND_ERROR_IS_FATAL ANY
)
if(NERI_NINJA_VERSION VERSION_LESS "1.11")
  message(FATAL_ERROR "Neri requires Ninja 1.11 or newer; found ${NERI_NINJA_VERSION}.")
endif()

foreach(NERI_LANGUAGE C CXX)
  if(NOT CMAKE_${NERI_LANGUAGE}_COMPILER_ID STREQUAL "Clang" OR
     NOT CMAKE_${NERI_LANGUAGE}_COMPILER_VERSION VERSION_EQUAL NERI_LLVM_VERSION)
    message(FATAL_ERROR "Neri requires Clang ${NERI_LLVM_VERSION} for ${NERI_LANGUAGE}.")
  endif()
endforeach()

if(APPLE)
  if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
    message(FATAL_ERROR "Neri supports macOS ARM64 only on Apple hosts.")
  endif()
  set(NERI_TARGET "macos-arm64")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
    message(FATAL_ERROR "Neri supports Linux x86-64 only on Linux hosts.")
  endif()
  set(NERI_TARGET "linux-x86_64")
elseif(WIN32 AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
  set(NERI_TARGET "windows-x86_64")
else()
  message(FATAL_ERROR "Neri supports macOS ARM64, Linux x86-64 and Windows x86-64.")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_COMPILE_WARNING_AS_ERROR ON)

add_library(neri_build_options INTERFACE)
target_compile_options(neri_build_options INTERFACE -Wall -Wextra -Wpedantic)
if(WIN32)
  target_compile_definitions(neri_build_options INTERFACE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)
endif()

find_package(LLVM ${NERI_LLVM_VERSION} EXACT CONFIG REQUIRED)
if(WIN32 AND TARGET LLVMDebugInfoPDB)
  # LLVM's Windows archive records the release builder's absolute DIA SDK path.
  find_library(NERI_DIAGUIDS diaguids HINTS "$ENV{VSINSTALLDIR}/DIA SDK/lib/amd64" REQUIRED)
  get_target_property(NERI_PDB_LIBRARIES LLVMDebugInfoPDB INTERFACE_LINK_LIBRARIES)
  list(FILTER NERI_PDB_LIBRARIES EXCLUDE REGEX "[/\\\\]diaguids\\.lib$")
  set_property(TARGET LLVMDebugInfoPDB PROPERTY INTERFACE_LINK_LIBRARIES "${NERI_DIAGUIDS};${NERI_PDB_LIBRARIES}")
endif()
if(NOT LLVM_PACKAGE_VERSION VERSION_EQUAL NERI_LLVM_VERSION)
  message(FATAL_ERROR "Neri requires LLVM ${NERI_LLVM_VERSION}; found ${LLVM_PACKAGE_VERSION}.")
endif()

if(APPLE)
  foreach(NERI_LLVM_DEPENDENCY_TARGET ZLIB::ZLIB LibEdit::LibEdit FFI::ffi FFI::ffi_static)
    if(TARGET "${NERI_LLVM_DEPENDENCY_TARGET}")
      get_target_property(NERI_INCLUDE_DIRS "${NERI_LLVM_DEPENDENCY_TARGET}" INTERFACE_INCLUDE_DIRECTORIES)
      if(NERI_INCLUDE_DIRS)
        list(FILTER NERI_INCLUDE_DIRS EXCLUDE REGEX "/MacOSX[^/]*\\.sdk/usr/include$")
        set_target_properties("${NERI_LLVM_DEPENDENCY_TARGET}" PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${NERI_INCLUDE_DIRS}")
      endif()
    endif()
  endforeach()
endif()

llvm_map_components_to_libnames(NERI_LLVM_LIBRARIES Core Passes Support Target AArch64 X86)
option(NERI_SANITIZERS "Instrument native components with address and undefined-behavior sanitizers" OFF)
option(NERI_THREAD_SANITIZER "Instrument runtime heap concurrency with ThreadSanitizer" OFF)
if(NERI_SANITIZERS AND NERI_THREAD_SANITIZER)
  message(FATAL_ERROR "AddressSanitizer and ThreadSanitizer require separate builds.")
endif()
if(NERI_SANITIZERS AND APPLE)
  # Keep non-instrumented Homebrew LLVM's libc++ templates within its dylib.
  # Mixing its static templates with ASan-instrumented templates poisons live containers.
  set(NERI_LLVM_LIBRARIES LLVM)
endif()

find_program(NERI_LLVM_AR NAMES llvm-ar HINTS "${LLVM_TOOLS_BINARY_DIR}" REQUIRED NO_DEFAULT_PATH)
find_program(NERI_LLVM_RANLIB NAMES llvm-ranlib HINTS "${LLVM_TOOLS_BINARY_DIR}" REQUIRED NO_DEFAULT_PATH)
set(CMAKE_AR "${NERI_LLVM_AR}")
set(CMAKE_RANLIB "${NERI_LLVM_RANLIB}")
set(CMAKE_AR "${NERI_LLVM_AR}" CACHE FILEPATH "Pinned deterministic archiver" FORCE)
set(CMAKE_RANLIB "${NERI_LLVM_RANLIB}" CACHE FILEPATH "Pinned deterministic archive indexer" FORCE)
foreach(NERI_LANGUAGE C CXX)
  set(CMAKE_${NERI_LANGUAGE}_ARCHIVE_CREATE "<CMAKE_AR> qcD <TARGET> <LINK_FLAGS> <OBJECTS>")
  set(CMAKE_${NERI_LANGUAGE}_ARCHIVE_APPEND "<CMAKE_AR> qD <TARGET> <LINK_FLAGS> <OBJECTS>")
  set(CMAKE_${NERI_LANGUAGE}_ARCHIVE_FINISH "<CMAKE_RANLIB> -D <TARGET>")
endforeach()

add_library(neri_codegen STATIC
  native/codegen/ir_reader.cpp
  native/codegen/ir_verifier.cpp
  native/codegen/llvm_emitter.cpp
  native/codegen/llvm_lowering.cpp
)
target_include_directories(neri_codegen PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/native/include")
target_include_directories(neri_codegen SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
target_link_libraries(neri_codegen PUBLIC neri_build_options PRIVATE ${NERI_LLVM_LIBRARIES})

add_executable(neri_codegen_cli native/codegen/main.cpp)
set_target_properties(neri_codegen_cli PROPERTIES OUTPUT_NAME neri-codegen)
target_compile_definitions(neri_codegen_cli PRIVATE NERI_TOOLCHAIN_VERSION="${NERI_VERSION}")
target_link_libraries(neri_codegen_cli PRIVATE neri_build_options neri_codegen ${NERI_LLVM_LIBRARIES})

find_package(Threads REQUIRED)
if(WIN32)
  set(NERI_PLATFORM_SOURCES native/platform/socket_windows.c native/platform/terminal_windows.c native/platform/windows_seed_compat.cpp)
else()
  set(NERI_PLATFORM_SOURCES native/platform/socket_posix.c native/platform/terminal_posix.c)
endif()
add_library(neri_runtime STATIC
  native/platform/interrupts.cpp
  native/platform/files.cpp
  native/platform/host.cpp
  native/runtime/entry.cpp
  native/runtime/math.cpp
  native/runtime/runtime.cpp
  native/runtime/task_executor.cpp
  ${NERI_PLATFORM_SOURCES}
  native/runtime/test_support.cpp
)
set_target_properties(neri_runtime PROPERTIES OUTPUT_NAME "neri-runtime")
target_compile_definitions(neri_runtime PRIVATE NERI_RUNTIME_BUILD=1)
target_compile_definitions(neri_runtime PUBLIC NERI_RUNTIME_STATIC=1)
if(WIN32)
  target_link_libraries(neri_runtime PUBLIC ws2_32 bcrypt shell32)
endif()
target_link_libraries(neri_runtime PRIVATE neri_build_options Threads::Threads)
target_include_directories(neri_runtime PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/native/include")
if(NERI_THREAD_SANITIZER)
  target_compile_options(neri_runtime PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
  target_link_options(neri_runtime INTERFACE -fsanitize=thread)
endif()

if(NERI_SANITIZERS)
  target_compile_options(neri_build_options INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(neri_build_options INTERFACE -fsanitize=address,undefined)
endif()

include(CTest)
add_executable(neri_host native/tools/host.cpp)
set_target_properties(neri_host PROPERTIES OUTPUT_NAME neri-host)
target_link_libraries(neri_host PRIVATE neri_build_options)
if(WIN32)
  target_link_libraries(neri_host PRIVATE shell32)
  target_link_libraries(neri_codegen_cli PRIVATE shell32)
endif()
target_include_directories(neri_host PRIVATE native/include)
if(WIN32)
  add_executable(neri_launcher native/tools/launcher_windows.cpp)
  set_target_properties(neri_launcher PROPERTIES OUTPUT_NAME neri)
  target_include_directories(neri_launcher PRIVATE native/include)
  target_link_libraries(neri_launcher PRIVATE neri_build_options shell32)
endif()
if(BUILD_TESTING)
  add_executable(neri_platform_host_tests tests/native/platform_host_tests.cpp)
  target_link_libraries(neri_platform_host_tests PRIVATE neri_build_options neri_runtime)
  add_test(NAME native_platform_host COMMAND neri_platform_host_tests)
  set_tests_properties(native_platform_host PROPERTIES TIMEOUT 20)
  if(WIN32)
    add_executable(neri_windows_platform_test tests/native/windows_platform.c)
    target_link_libraries(neri_windows_platform_test PRIVATE neri_build_options neri_runtime user32)
    add_test(NAME native_windows_io COMMAND neri_windows_platform_test)
    set_tests_properties(native_windows_io PROPERTIES TIMEOUT 20)
    find_program(NERI_POWERSHELL NAMES pwsh REQUIRED)
    add_test(NAME native_windows_process COMMAND "${NERI_POWERSHELL}" -NoProfile -File
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/windows-host.ps1" -HostExecutable "$<TARGET_FILE:neri_host>")
    set_tests_properties(native_windows_process PROPERTIES TIMEOUT 20)
  endif()
  add_library(neri_ffi_contract SHARED tests/native/ffi_contract.c)
  set_target_properties(neri_ffi_contract PROPERTIES OUTPUT_NAME "neri-ffi-contract" WINDOWS_EXPORT_ALL_SYMBOLS ON)
  target_link_libraries(neri_ffi_contract PRIVATE neri_build_options)
  if(NOT WIN32)
  add_executable(neri_lsp_test tests/lsp/protocol.c)
  set_target_properties(neri_lsp_test PROPERTIES OUTPUT_NAME neri-lsp-test)
  target_link_libraries(neri_lsp_test PRIVATE neri_build_options)
  add_executable(neri_http_process tests/native/http_process.c)
  set_target_properties(neri_http_process PROPERTIES OUTPUT_NAME neri-http-process)
  target_link_libraries(neri_http_process PRIVATE neri_build_options)
  add_executable(neri_terminal_process tests/native/terminal_process.c)
  set_target_properties(neri_terminal_process PROPERTIES OUTPUT_NAME neri-terminal-process)
  target_link_libraries(neri_terminal_process PRIVATE neri_build_options util)
  endif()

  add_executable(neri_abi_probe tests/native/abi_probe.c)
  target_link_libraries(neri_abi_probe PRIVATE neri_build_options neri_runtime)
  add_test(NAME native_abi COMMAND neri_abi_probe)

  add_executable(neri_string_probe tests/native/string_runtime_probe.cpp)
  target_link_libraries(neri_string_probe PRIVATE neri_build_options neri_runtime)
  add_test(NAME native_strings COMMAND neri_string_probe)

  add_executable(neri_gc_pressure tests/native/gc_pressure.cpp)
  target_link_libraries(neri_gc_pressure PRIVATE neri_build_options neri_runtime)
  add_test(NAME native_gc_pressure COMMAND neri_gc_pressure)

  if(NOT WIN32)
  add_executable(neri_worker_heaps tests/native/worker_heaps.c)
  target_include_directories(neri_worker_heaps PRIVATE native/runtime)
  target_link_libraries(neri_worker_heaps PRIVATE neri_build_options neri_runtime Threads::Threads)
  if(NERI_THREAD_SANITIZER)
    target_compile_options(neri_worker_heaps PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
  endif()
  add_test(NAME native_worker_heaps COMMAND neri_worker_heaps)
  set_tests_properties(native_worker_heaps PROPERTIES TIMEOUT 30)
  endif()

  add_executable(neri_codegen_tests tests/native/codegen_tests.cpp)
  target_include_directories(neri_codegen_tests SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
  target_link_libraries(neri_codegen_tests PRIVATE neri_build_options neri_codegen ${NERI_LLVM_LIBRARIES})
  add_test(NAME native_ir_boundary COMMAND neri_codegen_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/ir-minimal-v1.0.nir.hex"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/ir-codegen-primitives.nir.hex")
endif()

set(NERI_MANIFEST_VERSION "${NERI_VERSION}")
set(NERI_MANIFEST_NAME "neri-runtime-${NERI_TARGET}.json")
  file(GENERATE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${NERI_MANIFEST_NAME}"
    CONTENT "{\n  \"schemaVersion\": 2,\n  \"toolchainVersion\": \"${NERI_MANIFEST_VERSION}\",\n  \"irTransportMajor\": 1,\n  \"irTransportMinor\": 1,\n  \"target\": \"${NERI_TARGET}\",\n  \"abiMajor\": 1,\n  \"abiMinor\": 12,\n  \"features\": 129791,\n  \"artifact\": \"$<TARGET_FILE_NAME:neri_runtime>\"\n}\n"
  )
install(TARGETS neri_codegen_cli neri_runtime RUNTIME DESTINATION bin ARCHIVE DESTINATION lib)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/neri-runtime-${NERI_TARGET}.json" DESTINATION lib)
