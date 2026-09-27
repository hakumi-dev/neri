#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
case "${1:-}" in
  native) shift; exec "$ROOT_DIR/scripts/build-native.sh" "$@" ;;
  native-test) shift; exec "$ROOT_DIR/scripts/build-native.sh" --test "$@" ;;
esac
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
(
  cd "$NATIVE_DIR"
  shasum -a 256 neri-codegen neri-host libneri-runtime.a "neri-runtime-$TARGET.json"
) > "$LAUNCH_DIR/native.sha256"
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
if [[ "$(sed -nE 's/^[[:space:]]*"schemaVersion"[[:space:]]*:[[:space:]]*([0-9]+),?$/\1/p' "$LAUNCH_DIR/PROVENANCE.json")" != 2 ||
      "$(seed_string format)" != neri-ir-bundle-gzip ||
      "$(seed_string artifact)" != compiler.nir.tar.gz ||
      "$(seed_string sourceManifest)" != SOURCE-MANIFEST.sha256 ]]; then
  echo "Unsupported canonical bootstrap seed metadata." >&2
  exit 2
fi
UNIT_COUNT="$(sed -nE 's/^[[:space:]]*"unitCount"[[:space:]]*:[[:space:]]*([0-9]+),?$/\1/p' "$LAUNCH_DIR/PROVENANCE.json")"
if [[ ! "$UNIT_COUNT" =~ ^[1-9][0-9]{0,3}$ ]] || (( UNIT_COUNT > 1024 )); then
  echo "Invalid bootstrap unit count." >&2
  exit 2
fi
BUNDLE="$LAUNCH_DIR/compiler.nir.tar.gz"
cp "$ROOT_DIR/bootstrap/compiler.nir.tar.gz" "$BUNDLE"
verify_digest "$BUNDLE" "$(seed_digest artifactSha256)"
verify_digest "$ROOT_DIR/bootstrap/SOURCE-MANIFEST.sha256" "$(seed_digest sourceManifestSha256)"
verify_digest "$ROOT_DIR/bootstrap/VALIDATION-SOURCE-MANIFEST.sha256" "$(seed_digest validationSourceManifestSha256)"
gzip -dc "$BUNDLE" > "$LAUNCH_DIR/compiler.nir.tar"
BUNDLE="$LAUNCH_DIR/compiler.nir.tar"
printf '%s\n' UNITS.sha256 > "$LAUNCH_DIR/expected.entries"
for ((unit = 0; unit < UNIT_COUNT; unit++)); do
  printf 'unit-%s.nir\n' "$unit" >> "$LAUNCH_DIR/expected.entries"
done
tar --ignore-zeros -tf "$BUNDLE" > "$LAUNCH_DIR/actual.entries"
if ! cmp -s "$LAUNCH_DIR/expected.entries" "$LAUNCH_DIR/actual.entries"; then
  echo "Bootstrap bundle entries do not match the unit count." >&2
  exit 2
fi
tar --ignore-zeros -tvf "$BUNDLE" > "$LAUNCH_DIR/entry.types"
entries=0
while IFS= read -r entry; do
  if [[ "$entry" != -* ]]; then
    echo "Bootstrap bundle members must be regular files." >&2
    exit 2
  fi
  entries=$((entries + 1))
done < "$LAUNCH_DIR/entry.types"
if (( entries != UNIT_COUNT + 1 )); then
  echo "Invalid bootstrap bundle member types." >&2
  exit 2
fi
tar --ignore-zeros -xOf "$BUNDLE" UNITS.sha256 > "$LAUNCH_DIR/UNITS.sha256"
verify_digest "$LAUNCH_DIR/UNITS.sha256" "$(seed_digest unitManifestSha256)"
: > "$LAUNCH_DIR/checked.units"
unit=0
while IFS= read -r row; do
  if (( unit >= UNIT_COUNT )) || [[ ! "$row" =~ ^([0-9a-f]{64})\ \ unit-([0-9]+)\.nir$ ]] ||
      [[ "${BASH_REMATCH[2]}" != "$unit" ]]; then
    echo "Invalid bootstrap unit manifest." >&2
    exit 2
  fi
  digest="${BASH_REMATCH[1]}"
  printf '%s  unit-%s.nir\n' "$digest" "$unit" >> "$LAUNCH_DIR/checked.units"
  tar --ignore-zeros -xOf "$BUNDLE" "unit-$unit.nir" > "$LAUNCH_DIR/unit-$unit.nir"
  unit=$((unit + 1))
done < "$LAUNCH_DIR/UNITS.sha256"
if (( unit != UNIT_COUNT )) || ! cmp -s "$LAUNCH_DIR/UNITS.sha256" "$LAUNCH_DIR/checked.units"; then
  echo "Invalid bootstrap unit manifest." >&2
  exit 2
fi
if ! (cd "$LAUNCH_DIR" && shasum -a 256 -c UNITS.sha256) > "$LAUNCH_DIR/unit-verification.log" 2>&1; then
  echo "Bootstrap unit checksum mismatch." >&2
  cat "$LAUNCH_DIR/unit-verification.log" >&2
  exit 2
fi
echo '[bootstrap] Materializing Stage0 from verified seed'
LINK_ARGUMENTS=()
for ((unit = 0; unit < UNIT_COUNT; unit++)); do
  "$NATIVE_DIR/neri-codegen" --input "$LAUNCH_DIR/unit-$unit.nir" --input-format binary \
    --target "$TARGET" --optimization release --emit object --output "$LAUNCH_DIR/unit-$unit.o" \
    --object-cache "$ROOT_DIR/build/cache/bootstrap-objects"
  LINK_ARGUMENTS+=("$LAUNCH_DIR/unit-$unit.o")
done
LINK_ARGUMENTS+=("$NATIVE_DIR/libneri-runtime.a" -o "$LAUNCH_DIR/bin/neri")
if [[ "$TARGET" == linux-x86_64 ]]; then LINK_ARGUMENTS+=(-lcrypto); fi
"$LLVM_PREFIX/bin/clang++" "${LINK_ARGUMENTS[@]}"

echo '[bootstrap] Preparing Neri build driver'
(umask 077; mkdir -p "$ROOT_DIR/build/cache/compiler")
env -i "PATH=$PATH" "HOME=$HOME" LC_ALL=C LANG=C TZ=UTC "SDKROOT=$SDKROOT" "DEVELOPER_DIR=${DEVELOPER_DIR:-}" \
  "NERI_CACHE_DIR=$ROOT_DIR/build/cache/compiler" \
  "NERI_STDLIB=$ROOT_DIR/stdlib" "NERI_LIBRARY_PATH=$NATIVE_DIR" \
  "NERI_HOST=$NATIVE_DIR/neri-host" "NERI_CODEGEN=$NATIVE_DIR/neri-codegen" \
  "NERI_RUNTIME_MANIFEST=$NATIVE_DIR/neri-runtime-$TARGET.json" \
  "NERI_LINKER=$LLVM_PREFIX/bin/clang++" \
  "$LAUNCH_DIR/bin/neri" build --project "$ROOT_DIR/manifest.json" --unit build \
  --source-root "$ROOT_DIR" --module neri-build --target "$TARGET" --release --timings \
  --output "$LAUNCH_DIR/neri-build"
env -i "PATH=$PATH" "HOME=$HOME" LC_ALL=C LANG=C TZ=UTC "SDKROOT=$SDKROOT" "DEVELOPER_DIR=${DEVELOPER_DIR:-}" \
  "NERI_ROOT=$ROOT_DIR" "NERI_SEED_DIR=$LAUNCH_DIR" "LLVM_PREFIX=$LLVM_PREFIX" "NERI_TARGET=$TARGET" \
  "NERI_NATIVE_READY=$ROOT_DIR|release" "NERI_NATIVE_READY_MANIFEST=$LAUNCH_DIR/native.sha256" \
  "$LAUNCH_DIR/neri-build" "$@"
