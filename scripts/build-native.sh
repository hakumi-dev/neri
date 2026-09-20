#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
MODE=release
BUILD_TYPE=Release
SANITIZERS=OFF
THREAD_SANITIZER=OFF
if [[ $# -gt 1 ]]; then echo "Expected at most one native build mode." >&2; exit 2; fi
case "${1:-}" in
  '') ;;
  --debug) MODE=debug; BUILD_TYPE=Debug ;;
  --sanitize) MODE=sanitize; BUILD_TYPE=Debug; SANITIZERS=ON ;;
  --thread-sanitize) MODE=thread-sanitize; BUILD_TYPE=Debug; THREAD_SANITIZER=ON ;;
  *) echo "Unknown native build mode: $1" >&2; exit 2 ;;
esac
case "$(uname -s):$(uname -m)" in
  Darwin:arm64) LLVM_PREFIX="${LLVM_PREFIX:-$(brew --prefix llvm@22)}" ;;
  Linux:x86_64) LLVM_PREFIX="${LLVM_PREFIX:-/usr/lib/llvm-22}" ;;
  *) echo "Unsupported native build host." >&2; exit 2 ;;
esac
cmake -S "$ROOT_DIR" -B "$ROOT_DIR/build/native/native-$MODE" -G Ninja \
  "-DCMAKE_BUILD_TYPE=$BUILD_TYPE" \
  "-DCMAKE_C_COMPILER=$LLVM_PREFIX/bin/clang" \
  "-DCMAKE_CXX_COMPILER=$LLVM_PREFIX/bin/clang++" \
  "-DLLVM_DIR=$LLVM_PREFIX/lib/cmake/llvm" -DBUILD_TESTING=ON \
  "-DNERI_SANITIZERS=$SANITIZERS" "-DNERI_THREAD_SANITIZER=$THREAD_SANITIZER"
cmake --build "$ROOT_DIR/build/native/native-$MODE"
