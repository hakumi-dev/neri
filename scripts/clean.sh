#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
BUILD_DIR="$ROOT_DIR/build"
for option in "$@"; do
  case "$option" in --dry-run|--cache|--legacy) ;; *) echo "Unknown clean option: $option" >&2; exit 2 ;; esac
done
if [[ ! -d "$BUILD_DIR" ]]; then echo '[clean] Nothing to remove.'; exit 0; fi
if [[ -L "$BUILD_DIR" || -L "$BUILD_DIR/.leases" ]]; then
  echo 'Build directories must not be symbolic links.' >&2; exit 2
fi
if [[ ! -x "$BUILD_DIR/current/bin/neri" ]]; then
  echo 'Clean requires an existing build/current compiler; it never bootstraps one.' >&2; exit 2
fi
if ! mkdir "$BUILD_DIR/.maintenance" 2>/dev/null; then
  echo 'Build maintenance or another launcher is starting; retry shortly.' >&2; exit 2
fi
CLEAN_WORK=""
CLEAN_CHILD=""
finish_clean() {
  if [[ -n "$CLEAN_WORK" ]]; then rm -rf -- "$CLEAN_WORK"; fi
  rmdir "$BUILD_DIR/.maintenance"
}
trap finish_clean EXIT
interrupt_clean() {
  if [[ -n "$CLEAN_CHILD" ]]; then
    kill "$CLEAN_CHILD" 2>/dev/null || true
    wait "$CLEAN_CHILD" 2>/dev/null || true
  fi
  exit 130
}
trap interrupt_clean INT TERM HUP
CLEAN_TMP="${TMPDIR:-/tmp}"
CLEAN_WORK="$(mktemp -d "${CLEAN_TMP%/}/neri-clean.XXXXXX")"
shopt -s nullglob
for lease in "$BUILD_DIR/.leases/"*; do
  pid="${lease##*/}"
  if [[ ! "$pid" =~ ^[0-9]+$ || -L "$lease" || ! -f "$lease" ]]; then
    echo "Invalid build lease: $lease" >&2; exit 2
  fi
  if kill -0 "$pid" 2>/dev/null; then
    echo "Build process $pid is active; clean did not remove any artifacts." >&2; exit 2
  fi
done
TOOLCHAIN_DIR="$(CDPATH= cd -- "$BUILD_DIR/current" && pwd -P)"
printf '%s\n' "$TOOLCHAIN_DIR" > "$CLEAN_WORK/protected"
if [[ -e "$BUILD_DIR/.clean-keep" || -L "$BUILD_DIR/.clean-keep" ]]; then
  if [[ ! -f "$BUILD_DIR/.clean-keep" || -L "$BUILD_DIR/.clean-keep" ]]; then
    echo 'build/.clean-keep must be a regular file.' >&2; exit 2
  fi
  cat "$BUILD_DIR/.clean-keep" >> "$CLEAN_WORK/protected"
fi
# Both open paths and command arguments matter: an active worker may currently
# have no open files in its output directory. Do not infer inactivity from age.
lsof -n -P -a -u "$(id -u)" -F n > "$CLEAN_WORK/open" 2> "$CLEAN_WORK/lsof.stderr"
ps -axo command= > "$CLEAN_WORK/processes"
awk -v root="$BUILD_DIR/" 'substr($0,1,1)=="n" && index(substr($0,2),root)==1 {print substr($0,2)}' "$CLEAN_WORK/open" >> "$CLEAN_WORK/protected"
awk -v root="$BUILD_DIR/" '{for(i=1;i<=NF;i++) if(index($i,root)==1) print $i}' "$CLEAN_WORK/processes" >> "$CLEAN_WORK/protected"
if [[ " $* " == *' --cache '* ]]; then
  if grep -F "$BUILD_DIR/" "$CLEAN_WORK/processes" | grep -E '(^|/)(neri|neri-codegen|neri-host)( |$)' > /dev/null; then
    echo 'Cache cleanup requires local compiler, console and editor processes to be stopped.' >&2; exit 2
  fi
fi
case "$(uname -s)" in
  Darwin) LLVM_PREFIX="${LLVM_PREFIX:-$(brew --prefix llvm@22)}"; SDKROOT="$(xcrun --show-sdk-path)" ;;
  Linux) LLVM_PREFIX="${LLVM_PREFIX:-/usr/lib/llvm-22}"; SDKROOT="" ;;
  *) echo 'Clean is supported by the macOS and Linux launchers.' >&2; exit 2 ;;
esac
env "SDKROOT=$SDKROOT" "NERI_CODEGEN=$TOOLCHAIN_DIR/bin/neri-codegen" \
  "NERI_RUNTIME_MANIFEST=$TOOLCHAIN_DIR/lib/neri-runtime.json" \
  "NERI_LINKER=$LLVM_PREFIX/bin/clang++" "NERI_STDLIB=$ROOT_DIR/stdlib" \
  "$TOOLCHAIN_DIR/bin/neri" run "$ROOT_DIR/tooling/clean.hk" -- \
  "$BUILD_DIR" "$CLEAN_WORK/protected" "$@" &
CLEAN_CHILD=$!
wait "$CLEAN_CHILD"
CLEAN_CHILD=""
# Expired leases are metadata, not evidence of live work.
if [[ " $* " != *' --dry-run '* ]]; then
  for lease in "$BUILD_DIR/.leases/"*; do rm -- "$lease"; done
fi
