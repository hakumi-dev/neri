#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
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
export SDKROOT

# Native components materialize the same canonical seed on every host.
LLVM_PREFIX="$LLVM_PREFIX" "$ROOT_DIR/scripts/build-native.sh"
NATIVE_DIR="$ROOT_DIR/build/native/native-release"
LAUNCH_DIR="$(mktemp -d "$ROOT_DIR/build/launcher.XXXXXX")"
trap 'rm -rf "$LAUNCH_DIR"' EXIT
mkdir -p "$LAUNCH_DIR/bin"
cp "$ROOT_DIR/bootstrap/seed.json" "$LAUNCH_DIR/PROVENANCE.json"

seed_digest() {
  local value
  value="$(sed -nE 's/^[[:space:]]*"'"$1"'"[[:space:]]*:[[:space:]]*"([0-9a-f]{64})",?$/\1/p' "$LAUNCH_DIR/PROVENANCE.json")"
  if [[ ! "$value" =~ ^[0-9a-f]{64}$ ]]; then
    echo "Invalid bootstrap digest: $1" >&2
    exit 2
  fi
  printf '%s' "$value"
}
seed_string() {
  sed -nE 's/^[[:space:]]*"'"$1"'"[[:space:]]*:[[:space:]]*"([^"[:cntrl:]]*)",?$/\1/p' "$LAUNCH_DIR/PROVENANCE.json"
}
verify_digest() {
  local actual
  actual="$(shasum -a 256 "$1" | awk '{print $1}')"
  if [[ "$actual" != "$2" ]]; then echo "Bootstrap checksum mismatch: $1" >&2; exit 2; fi
}
if [[ "$(sed -nE 's/^[[:space:]]*"schemaVersion"[[:space:]]*:[[:space:]]*([0-9]+),?$/\1/p' "$LAUNCH_DIR/PROVENANCE.json")" != 1 ||
      "$(seed_string format)" != neri-ir-binary-gzip ||
      "$(seed_string artifact)" != compiler.nir.gz ||
      "$(seed_string sourceManifest)" != SOURCE-MANIFEST.sha256 ]]; then
  echo "Unsupported canonical bootstrap seed metadata." >&2
  exit 2
fi
verify_digest "$ROOT_DIR/bootstrap/compiler.nir.gz" "$(seed_digest artifactSha256)"
verify_digest "$ROOT_DIR/bootstrap/SOURCE-MANIFEST.sha256" "$(seed_digest sourceManifestSha256)"
verify_digest "$ROOT_DIR/bootstrap/VALIDATION-SOURCE-MANIFEST.sha256" "$(seed_digest validationSourceManifestSha256)"
gzip -dc "$ROOT_DIR/bootstrap/compiler.nir.gz" > "$LAUNCH_DIR/compiler.nir"
verify_digest "$LAUNCH_DIR/compiler.nir" "$(seed_digest irSha256)"
"$NATIVE_DIR/neri-codegen" --input "$LAUNCH_DIR/compiler.nir" --input-format binary \
  --target "$TARGET" --optimization release --emit object --output "$LAUNCH_DIR/compiler.o"
LINK_ARGUMENTS=("$LAUNCH_DIR/compiler.o" "$NATIVE_DIR/libneri-runtime.a" -o "$LAUNCH_DIR/bin/neri")
if [[ "$TARGET" == linux-x86_64 ]]; then LINK_ARGUMENTS+=(-lcrypto); fi
"$LLVM_PREFIX/bin/clang++" "${LINK_ARGUMENTS[@]}"

env -i "PATH=$PATH" "HOME=$HOME" LC_ALL=C LANG=C TZ=UTC "SDKROOT=$SDKROOT" "DEVELOPER_DIR=${DEVELOPER_DIR:-}" \
  "NERI_STDLIB=$ROOT_DIR/stdlib" "NERI_LIBRARY_PATH=$NATIVE_DIR" \
  "NERI_HOST=$NATIVE_DIR/neri-host" "NERI_CODEGEN=$NATIVE_DIR/neri-codegen" \
  "NERI_RUNTIME_MANIFEST=$NATIVE_DIR/neri-runtime-$TARGET.json" \
  "NERI_LINKER=$LLVM_PREFIX/bin/clang++" \
  "$LAUNCH_DIR/bin/neri" build --project "$ROOT_DIR/manifest.json" --unit build \
  --source-root "$ROOT_DIR" --module neri-build --target "$TARGET" --release \
  --output "$LAUNCH_DIR/neri-build"
env -i "PATH=$PATH" "HOME=$HOME" LC_ALL=C LANG=C TZ=UTC "SDKROOT=$SDKROOT" "DEVELOPER_DIR=${DEVELOPER_DIR:-}" \
  "NERI_ROOT=$ROOT_DIR" "NERI_SEED_DIR=$LAUNCH_DIR" "LLVM_PREFIX=$LLVM_PREFIX" "NERI_TARGET=$TARGET" \
  "$LAUNCH_DIR/neri-build" "$@"
