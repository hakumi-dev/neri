#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
if [[ ! -x "$ROOT_DIR/build/current/bin/neri" ]]; then "$ROOT_DIR/scripts/bootstrap.sh"; fi
TOOLCHAIN_DIR="$(CDPATH= cd -- "$ROOT_DIR/build/current" && pwd -P)"
LLVM_PREFIX="${LLVM_PREFIX:-$(brew --prefix llvm@22)}"
LINKER="${NERI_LINKER:-$LLVM_PREFIX/bin/clang++}"

exec env \
  "SDKROOT=$(xcrun --show-sdk-path)" \
  "NERI_CODEGEN=$TOOLCHAIN_DIR/bin/neri-codegen" \
  "NERI_RUNTIME_MANIFEST=$TOOLCHAIN_DIR/lib/neri-runtime.json" \
  "NERI_LINKER=$LINKER" \
  "$TOOLCHAIN_DIR/bin/neri" "$@"
