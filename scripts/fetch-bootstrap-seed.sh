#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"

case "$(uname -s):$(uname -m)" in
  Darwin:arm64) TARGET="macos-arm64" ;;
  Linux:x86_64) TARGET="linux-x86_64" ;;
  *)
    echo "No trusted bootstrap seed is published for this host." >&2
    exit 2
    ;;
esac

SEED_FILE="$ROOT_DIR/bootstrap/$TARGET.seed"
if [[ ! -f "$SEED_FILE" || ! -f "$ROOT_DIR/bootstrap/$TARGET.files.sha256" ]]; then
  echo "No trusted bootstrap seed is published for $TARGET." >&2
  exit 2
fi
REPOSITORY="$(sed -n 's/^repository=//p' "$SEED_FILE")"
TAG="$(sed -n 's/^tag=//p' "$SEED_FILE")"
ASSET="$(sed -n 's/^asset=//p' "$SEED_FILE")"
EXPECTED_SHA256="$(sed -n 's/^sha256=//p' "$SEED_FILE")"
SEED_DIR="$ROOT_DIR/.bootstrap/$TARGET"

if [[ -x "$SEED_DIR/bin/neri" ]]; then
  (cd "$SEED_DIR" && shasum -a 256 -c "$ROOT_DIR/bootstrap/$TARGET.files.sha256") >/dev/null
  printf '%s\n' "$SEED_DIR"
  exit 0
fi

command -v curl >/dev/null || { echo "curl is required to fetch the seed release." >&2; exit 2; }
mkdir -p "$ROOT_DIR/.bootstrap/downloads"
ARCHIVE="$ROOT_DIR/.bootstrap/downloads/$ASSET"

echo "[bootstrap] Downloading $REPOSITORY $TAG ($TARGET)" >&2
curl --fail --location --retry 3 \
  "https://github.com/$REPOSITORY/releases/download/$TAG/$ASSET" --output "$ARCHIVE"

ACTUAL_SHA256="$(shasum -a 256 "$ARCHIVE" | awk '{print $1}')"
if [[ "$ACTUAL_SHA256" != "$EXPECTED_SHA256" ]]; then
  echo "Bootstrap seed checksum mismatch: expected $EXPECTED_SHA256, found $ACTUAL_SHA256." >&2
  exit 1
fi

EXTRACT_DIR="$(mktemp -d "$ROOT_DIR/.bootstrap/.extract.XXXXXX")"
trap 'rm -rf "$EXTRACT_DIR"' EXIT
tar -xzf "$ARCHIVE" -C "$EXTRACT_DIR"
EXTRACTED="$EXTRACT_DIR/neri-bootstrap-seed-v1-$TARGET"
for REQUIRED in bin/neri bin/neri-codegen lib/neri-runtime-$TARGET.json lib/libneri-runtime.a PROVENANCE.json SOURCE-MANIFEST.sha256; do
  if [[ ! -f "$EXTRACTED/$REQUIRED" ]]; then
    echo "Bootstrap seed is missing $REQUIRED." >&2
    exit 1
  fi
done
(cd "$EXTRACTED" && shasum -a 256 -c "$ROOT_DIR/bootstrap/$TARGET.files.sha256") >/dev/null

rm -rf "$SEED_DIR"
mv "$EXTRACTED" "$SEED_DIR"
printf '%s\n' "$SEED_DIR"
