#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
STATE_DIR="$ROOT_DIR/build/local-ci"
mkdir -p "$STATE_DIR"
echo '[local-ci] Preparing the Neri runner...' >&2
"$ROOT_DIR/scripts/neri.sh" build --project "$ROOT_DIR/manifest.json" --unit local-ci --output "$STATE_DIR/neri-local-ci"
exec env \
  "NERI_CI_ASSET_DIR=$ROOT_DIR/tooling/ci" \
  "NERI_CI_STATE_DIR=$STATE_DIR" \
  "NERI_CI_SOURCE_ROOT=$ROOT_DIR" \
  "$STATE_DIR/neri-local-ci" "$@"
