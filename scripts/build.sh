#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
SEED_DIR="$("$ROOT_DIR/scripts/fetch-bootstrap-seed.sh")"
LLVM_PREFIX="${LLVM_PREFIX:-$(brew --prefix llvm@22)}"
SDKROOT="$(xcrun --show-sdk-path)"
mkdir -p "$ROOT_DIR/build"
LAUNCH_DIR="$(mktemp -d "$ROOT_DIR/build/launcher.XXXXXX")"
trap 'rm -rf "$LAUNCH_DIR"' EXIT
env -i "PATH=$PATH" "HOME=$HOME" LC_ALL=C LANG=C TZ=UTC "SDKROOT=$SDKROOT" \
  "NERI_CODEGEN=$SEED_DIR/bin/neri-codegen" \
  "NERI_RUNTIME_MANIFEST=$SEED_DIR/lib/neri-runtime-macos-arm64.json" \
  "NERI_LINKER=$LLVM_PREFIX/bin/clang++" \
  "$SEED_DIR/bin/neri" build "$ROOT_DIR/tooling/build.hk" "$ROOT_DIR/tooling/common.hk" "$ROOT_DIR/compiler/ir/process.hk" \
  --source-root "$ROOT_DIR" --module neri-build --target macos-arm64 --release \
  --output "$LAUNCH_DIR/neri-build"
env -i "PATH=$PATH" "HOME=$HOME" LC_ALL=C LANG=C TZ=UTC "SDKROOT=$SDKROOT" \
  "NERI_ROOT=$ROOT_DIR" "NERI_SEED_DIR=$SEED_DIR" "LLVM_PREFIX=$LLVM_PREFIX" \
  "$LAUNCH_DIR/neri-build" "$@"
