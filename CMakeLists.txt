cmake_minimum_required(VERSION 3.28)

project(NeriNative LANGUAGES C CXX)

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
else()
  message(FATAL_ERROR "Neri supports macOS ARM64 and Linux x86-64.")
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

find_package(LLVM ${NERI_LLVM_VERSION} EXACT CONFIG REQUIRED)
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

add_library(neri_runtime STATIC
  native/runtime/entry.cpp
  native/runtime/math.cpp
  native/runtime/runtime.cpp
  native/runtime/socket.c
  native/runtime/terminal.c
  native/runtime/test_support.cpp
)
set_target_properties(neri_runtime PROPERTIES OUTPUT_NAME "neri-runtime")
target_compile_definitions(neri_runtime PRIVATE NERI_RUNTIME_BUILD=1)
target_link_libraries(neri_runtime PRIVATE neri_build_options)
target_include_directories(neri_runtime PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/native/include")

if(NERI_SANITIZERS)
  target_compile_options(neri_build_options INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(neri_build_options INTERFACE -fsanitize=address,undefined)
endif()

include(CTest)
add_executable(neri_host native/tools/host.cpp)
set_target_properties(neri_host PROPERTIES OUTPUT_NAME neri-host)
target_link_libraries(neri_host PRIVATE neri_build_options)
if(BUILD_TESTING)
  add_library(neri_ffi_contract SHARED tests/native/ffi_contract.c)
  set_target_properties(neri_ffi_contract PROPERTIES OUTPUT_NAME "neri-ffi-contract")
  target_link_libraries(neri_ffi_contract PRIVATE neri_build_options)
  add_executable(neri_http_process tests/native/http_process.c)
  set_target_properties(neri_http_process PROPERTIES OUTPUT_NAME neri-http-process)
  target_link_libraries(neri_http_process PRIVATE neri_build_options)
  add_executable(neri_terminal_process tests/native/terminal_process.c)
  set_target_properties(neri_terminal_process PROPERTIES OUTPUT_NAME neri-terminal-process)
  target_link_libraries(neri_terminal_process PRIVATE neri_build_options util)

  add_executable(neri_abi_probe tests/native/abi_probe.c)
  target_link_libraries(neri_abi_probe PRIVATE neri_build_options neri_runtime)
  add_test(NAME native_abi COMMAND neri_abi_probe)

  add_executable(neri_string_probe tests/native/string_runtime_probe.cpp)
  target_link_libraries(neri_string_probe PRIVATE neri_build_options neri_runtime)
  add_test(NAME native_strings COMMAND neri_string_probe)

  add_executable(neri_gc_pressure tests/native/gc_pressure.cpp)
  target_link_libraries(neri_gc_pressure PRIVATE neri_build_options neri_runtime)
  add_test(NAME native_gc_pressure COMMAND neri_gc_pressure)

  add_executable(neri_codegen_tests tests/native/codegen_tests.cpp)
  target_include_directories(neri_codegen_tests SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
  target_link_libraries(neri_codegen_tests PRIVATE neri_build_options neri_codegen ${NERI_LLVM_LIBRARIES})
  add_test(NAME native_ir_boundary COMMAND neri_codegen_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/ir-minimal-v1.0.nir.hex"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/ir-codegen-primitives.nir.hex")
endif()

foreach(NERI_MANIFEST_KIND current seed)
  set(NERI_MANIFEST_VERSION "${NERI_VERSION}")
  set(NERI_MANIFEST_NAME "neri-runtime-${NERI_TARGET}.json")
  if(NERI_MANIFEST_KIND STREQUAL "seed")
    # The pinned seed requires its original version label, not a different ABI.
    set(NERI_MANIFEST_VERSION "0.1.0-dev")
    set(NERI_MANIFEST_NAME "neri-runtime-seed-${NERI_TARGET}.json")
  endif()
  file(GENERATE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${NERI_MANIFEST_NAME}"
    CONTENT "{\n  \"schemaVersion\": 2,\n  \"toolchainVersion\": \"${NERI_MANIFEST_VERSION}\",\n  \"irTransportMajor\": 1,\n  \"irTransportMinor\": 1,\n  \"target\": \"${NERI_TARGET}\",\n  \"abiMajor\": 1,\n  \"abiMinor\": 9,\n  \"features\": 15103,\n  \"artifact\": \"$<TARGET_FILE_NAME:neri_runtime>\"\n}\n"
  )
endforeach()

install(TARGETS neri_codegen_cli neri_runtime RUNTIME DESTINATION bin ARCHIVE DESTINATION lib)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/neri-runtime-${NERI_TARGET}.json" DESTINATION lib)
