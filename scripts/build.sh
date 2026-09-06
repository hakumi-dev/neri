#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
SEED_DIR="$("$ROOT_DIR/scripts/fetch-bootstrap-seed.sh")"
case "$(uname -s):$(uname -m)" in
  Darwin:arm64)
    TARGET=macos-arm64
    LLVM_PREFIX="${LLVM_PREFIX:-$(brew --prefix llvm@22)}"
    SDKROOT="$(xcrun --show-sdk-path)"
    ;;
  Linux:x86_64)
    TARGET=linux-x86_64
    LLVM_PREFIX="${LLVM_PREFIX:-/usr/lib/llvm-22}"
    SDKROOT=""
    ;;
  *) echo "Unsupported bootstrap host." >&2; exit 2 ;;
esac
mkdir -p "$ROOT_DIR/build"
LAUNCH_DIR="$(mktemp -d "$ROOT_DIR/build/launcher.XXXXXX")"
SEED_COMPILER="$SEED_DIR/bin/neri"
SEED_MANIFEST="$SEED_DIR/lib/neri-runtime-$TARGET.json"
if [[ "$TARGET" == macos-arm64 ]]; then
  SEED_COMPILER="$SEED_DIR/libexec/neri"
  SEED_MANIFEST="$SEED_DIR/lib/neri-runtime.json"
fi
trap 'rm -rf "$LAUNCH_DIR"' EXIT
# The trusted seed predates project manifests. The build unit includes tooling
# helpers but not the installer entry point, plus the process support library.
BOOTSTRAP_SOURCES=("$ROOT_DIR/compiler/ir/process.hk")
while IFS= read -r -d '' source; do
  if [[ "$source" != "$ROOT_DIR/tooling/install.hk" ]]; then
    BOOTSTRAP_SOURCES+=("$source")
  fi
done < <(find "$ROOT_DIR/tooling" -mindepth 1 \
    \( -type d \( -name .git -o -name .neri -o -name .cache -o -name .idea \
      -o -name .bootstrap -o -name build -o -name out -o -name dist \
      -o -name target -o -name bin -o -exec test -f '{}/neri.json' \; \) -prune \) \
    -o \( -type f -name '*.hk' -print0 \))
env -i "PATH=$PATH" "HOME=$HOME" LC_ALL=C LANG=C TZ=UTC "SDKROOT=$SDKROOT" \
  "NERI_CODEGEN=$SEED_DIR/bin/neri-codegen" \
  "NERI_RUNTIME_MANIFEST=$SEED_MANIFEST" \
  "NERI_LINKER=$LLVM_PREFIX/bin/clang++" \
  "$SEED_COMPILER" build "${BOOTSTRAP_SOURCES[@]}" \
  --source-root "$ROOT_DIR" --module neri-build --target "$TARGET" --release \
  --output "$LAUNCH_DIR/neri-build"
env -i "PATH=$PATH" "HOME=$HOME" LC_ALL=C LANG=C TZ=UTC "SDKROOT=$SDKROOT" \
  "NERI_ROOT=$ROOT_DIR" "NERI_SEED_DIR=$SEED_DIR" "LLVM_PREFIX=$LLVM_PREFIX" "NERI_TARGET=$TARGET" \
  "$LAUNCH_DIR/neri-build" "$@"
