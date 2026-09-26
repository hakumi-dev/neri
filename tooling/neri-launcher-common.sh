#!/usr/bin/env bash

NERI_LAUNCHER_ROOT="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"

neri_launcher_prepare() {
  local command="$1"
  local platform
  local llvm_prefix

  platform="$(uname -s)"
  if [[ "$command" == session ]]; then
    case "$platform:$(uname -m)" in
      Darwin:arm64) NERI_LAUNCHER_TARGET=macos-arm64 ;;
      Linux:x86_64) NERI_LAUNCHER_TARGET=linux-x86_64 ;;
      *) echo "Unsupported session host." >&2; exit 2 ;;
    esac
  fi
  if [[ "$platform" == Darwin ]]; then
    if [[ -z "${LLVM_PREFIX:-}" ]] && ! command -v brew >/dev/null; then
      echo "Install LLVM 22.1.8 or set LLVM_PREFIX to its installation directory." >&2
      exit 2
    fi
    llvm_prefix="${LLVM_PREFIX:-$(brew --prefix llvm@22)}"
    export SDKROOT="${SDKROOT:-$(xcrun --show-sdk-path)}"
  else
    llvm_prefix="${LLVM_PREFIX:-/usr/lib/llvm-22}"
  fi
  if [[ ! -x "$llvm_prefix/bin/llvm-config" ]] || [[ "$("$llvm_prefix/bin/llvm-config" --version)" != 22.1.8 ]]; then
    echo "Neri requires LLVM 22.1.8." >&2
    exit 2
  fi

  NERI_LAUNCHER_ENV=(
    "NERI_HOST=$NERI_LAUNCHER_ROOT/libexec/neri-host"
    "NERI_CODEGEN=$NERI_LAUNCHER_ROOT/bin/neri-codegen"
    "NERI_RUNTIME_MANIFEST=$NERI_LAUNCHER_ROOT/lib/neri-runtime.json"
    "NERI_LINKER=$llvm_prefix/bin/clang++"
    "NERI_STDLIB=$NERI_LAUNCHER_ROOT/stdlib"
  )
  if [[ "$command" != compiler ]]; then
    NERI_LAUNCHER_ENV+=("NERI_LIBRARY_PATH=$NERI_LAUNCHER_ROOT/lib")
  fi
  if [[ "$command" != session ]]; then
    NERI_LAUNCHER_ENV+=("NERI_TEMPLATE_CATALOG=${NERI_TEMPLATE_CATALOG:-$NERI_LAUNCHER_ROOT/share/neri/templates/declarations.json}")
  fi
  if [[ "$command" == session ]]; then
    NERI_LAUNCHER_ENV+=(
      "NERI_SESSION_COMPILER=$NERI_LAUNCHER_ROOT/libexec/neri"
      "NERI_SESSION_WORK=${NERI_SESSION_WORK:-${TMPDIR:-/tmp}}"
      "NERI_TARGET=$NERI_LAUNCHER_TARGET"
    )
  fi
}

neri_launcher_exec() {
  local command="$1"
  local executable

  shift
  if [[ "$command" == compiler && $# == 1 && ( "$1" == --version || "$1" == version || "$1" == --help ) ]]; then
    exec "$NERI_LAUNCHER_ROOT/libexec/neri" "$@"
  fi
  neri_launcher_prepare "$command"
  case "$command" in
    compiler) executable=neri ;;
    session) executable=neri-session-console ;;
    data-generate) executable=neri-data-generate ;;
    data-migration) executable=neri-data-migration ;;
    *) echo "Unknown Neri launcher command: $command" >&2; exit 2 ;;
  esac
  exec env "${NERI_LAUNCHER_ENV[@]}" "$NERI_LAUNCHER_ROOT/libexec/$executable" "$@"
}
