#!/usr/bin/env bash
set -euo pipefail
LAUNCHER="${BASH_SOURCE[0]}"
while [[ -L "$LAUNCHER" ]]; do
  LINK_DIR="$(CDPATH= cd -- "$(dirname -- "$LAUNCHER")" && pwd -P)"
  LAUNCHER="$(readlink "$LAUNCHER")"
  [[ "$LAUNCHER" = /* ]] || LAUNCHER="$LINK_DIR/$LAUNCHER"
done
PACKAGE_ROOT="$(CDPATH= cd -- "$(dirname -- "$LAUNCHER")/.." && pwd -P)"
case "$(uname -s):$(uname -m)" in
  Darwin:arm64) TARGET=macos-arm64 ;;
  Linux:x86_64) TARGET=linux-x86_64 ;;
  *) echo "Unsupported session host." >&2; exit 2 ;;
esac
if [[ "$(uname -s)" == Darwin ]]; then
  if [[ -z "${LLVM_PREFIX:-}" ]] && ! command -v brew >/dev/null; then
    echo "Install LLVM 22.1.8 or set LLVM_PREFIX to its installation directory." >&2
    exit 2
  fi
  LLVM_PREFIX="${LLVM_PREFIX:-$(brew --prefix llvm@22)}"
  export SDKROOT="${SDKROOT:-$(xcrun --show-sdk-path)}"
else
  LLVM_PREFIX="${LLVM_PREFIX:-/usr/lib/llvm-22}"
fi
if [[ ! -x "$LLVM_PREFIX/bin/llvm-config" ]] || [[ "$("$LLVM_PREFIX/bin/llvm-config" --version)" != 22.1.8 ]]; then
  echo "Neri requires LLVM 22.1.8." >&2
  exit 2
fi
exec env "NERI_SESSION_COMPILER=$PACKAGE_ROOT/libexec/neri" \
  "NERI_SESSION_WORK=${NERI_SESSION_WORK:-${TMPDIR:-/tmp}}" \
  "NERI_TARGET=$TARGET" "NERI_LINKER=$LLVM_PREFIX/bin/clang++" \
  "NERI_HOST=$PACKAGE_ROOT/libexec/neri-host" \
  "NERI_CODEGEN=$PACKAGE_ROOT/bin/neri-codegen" \
  "NERI_RUNTIME_MANIFEST=$PACKAGE_ROOT/lib/neri-runtime.json" \
  "NERI_LIBRARY_PATH=$PACKAGE_ROOT/lib" \
  "NERI_STDLIB=$PACKAGE_ROOT/stdlib" \
  "$PACKAGE_ROOT/libexec/neri-session-console" "$@"
