#!/usr/bin/env bash
# Shared admission gate for build launchers and exclusive maintenance.
# ROOT_DIR is set by the caller. Exec'd children keep the same PID lease.
mkdir -p "$ROOT_DIR/build"
if [[ -L "$ROOT_DIR/build" || -L "$ROOT_DIR/build/.leases" ]]; then
  echo 'Build directories must not be symbolic links.' >&2
  exit 2
fi
lease_ready=false
for attempt in {1..50}; do
  if mkdir "$ROOT_DIR/build/.maintenance" 2>/dev/null; then lease_ready=true; break; fi
  sleep 0.02
done
if [[ "$lease_ready" != true ]]; then
  echo 'Build maintenance or another launcher is starting; retry shortly.' >&2
  exit 2
fi
if ! mkdir -p "$ROOT_DIR/build/.leases" || [[ -L "$ROOT_DIR/build/.leases/$$" ]] || ! printf '%s\n' "$$" > "$ROOT_DIR/build/.leases/$$"; then
  rmdir "$ROOT_DIR/build/.maintenance"
  exit 2
fi
rmdir "$ROOT_DIR/build/.maintenance"

# Register long-lived children before allowing maintenance, so an interrupted
# launcher cannot turn a surviving build driver into apparently abandoned work.
neri_run_leased() {
  local admitted=false child status=0
  for attempt in {1..50}; do
    if mkdir "$ROOT_DIR/build/.maintenance" 2>/dev/null; then admitted=true; break; fi
    sleep 0.02
  done
  if [[ "$admitted" != true ]]; then return 2; fi
  "$@" &
  child=$!
  if [[ -L "$ROOT_DIR/build/.leases/$child" ]] || ! printf '%s\n' "$child" > "$ROOT_DIR/build/.leases/$child"; then
    kill "$child" 2>/dev/null || true
    wait "$child" 2>/dev/null || true
    rmdir "$ROOT_DIR/build/.maintenance"
    return 2
  fi
  rmdir "$ROOT_DIR/build/.maintenance"
  wait "$child" || status=$?
  rm -f -- "$ROOT_DIR/build/.leases/$child"
  return "$status"
}
