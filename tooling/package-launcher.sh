#!/usr/bin/env bash
set -euo pipefail
LAUNCHER="${BASH_SOURCE[0]}"
while [[ -L "$LAUNCHER" ]]; do
  LINK_DIR="$(CDPATH= cd -- "$(dirname -- "$LAUNCHER")" && pwd -P)"
  LAUNCHER="$(readlink "$LAUNCHER")"
  [[ "$LAUNCHER" = /* ]] || LAUNCHER="$LINK_DIR/$LAUNCHER"
done
PACKAGE_ROOT="$(CDPATH= cd -- "$(dirname -- "$LAUNCHER")/.." && pwd -P)"
if [[ $# == 1 && ( "$1" == --version || "$1" == version || "$1" == --help ) ]]; then
  exec "$PACKAGE_ROOT/libexec/neri" "$@"
fi
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
exec env "NERI_CODEGEN=$PACKAGE_ROOT/bin/neri-codegen" \
  "NERI_RUNTIME_MANIFEST=$PACKAGE_ROOT/lib/neri-runtime.json" \
  "NERI_LINKER=$LLVM_PREFIX/bin/clang++" \
  "$PACKAGE_ROOT/libexec/neri" "$@"
