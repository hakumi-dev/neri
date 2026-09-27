#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$(realpath "${BASH_SOURCE[0]}")")/neri-launcher-common.sh"
neri_launcher_exec data-generate "$@"
