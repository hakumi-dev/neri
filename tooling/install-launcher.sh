#!/usr/bin/env bash
set -euo pipefail
PACKAGE_ROOT="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
exec env "NERI_PACKAGE_ROOT=$PACKAGE_ROOT" "$PACKAGE_ROOT/libexec/neri-install" "$@"
