#!/usr/bin/env bash
# Neri-managed macOS ARM64 provider. Only VMs recorded in request stateRoot are mutable.
set -euo pipefail
umask 077

provider=macos-tart
asset_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
action=${1:-}
shift || true
if [[ ${1:-} == --request-json && $# -eq 2 ]]; then request=$2; else
  printf '%s\n' 'usage: macos-tart.sh <doctor|plan|provision|start|exec|stop|destroy> --request-json FILE' >&2
  exit 2
fi
json() { jq -cn --arg p "$provider" --arg a "$action" --arg s "$1" --arg r "${2:-}" --arg i "${3:-}" --arg op "${5:-}" --arg ep "${6:-}" --arg ev "${7:-}" --argjson x "${4:-null}" '{schemaVersion:1,provider:$p,action:$a,status:$s} + (if $r == "" then {} else {reason:$r} end) + (if $i == "" then {} else {instanceId:$i} end) + (if $x == null then {} else {exitCode:$x} end) + (if $op == "" then {} else {stdoutPath:$op} end) + (if $ep == "" then {} else {stderrPath:$ep} end) + (if $ev == "" then {} else {evidencePath:$ev} end)'; }
fail() { json failed "$1" "${instance_id:-}"; exit 1; }
pending() { json pending "$1"; exit 0; }
valid_id() { [[ $1 =~ ^[a-z0-9][a-z0-9-]{0,70}$ ]]; }
need_file() { [[ -f $1 && ! -L $1 ]]; }

command -v jq >/dev/null || { printf '%s\n' '{"schemaVersion":1,"provider":"macos-tart","action":"unknown","status":"failed","reason":"jq is required"}'; exit 1; }
need_file "$request" || { json failed 'request JSON file missing'; exit 1; }
[[ $(jq -r '.schemaVersion // 0' "$request") == 1 ]] || { json failed 'request schemaVersion must be 1'; exit 1; }
[[ $(jq -r '.provider // ""' "$request") == "$provider" ]] || { json failed 'request provider mismatch'; exit 1; }
config=$(jq -r '.configPath // ""' "$request")
need_file "$config" || { json failed 'local-ci.json missing'; exit 1; }
[[ $(jq -r '.schemaVersion // 0' "$config") == 1 ]] || { json failed 'config schemaVersion must be 1'; exit 1; }
instance_id=$(jq -r '.instanceId // ""' "$request")
if [[ -n $instance_id ]]; then
  valid_id "$instance_id" || { json failed 'unsafe instanceId'; exit 1; }
  instance_name="neri-ci-$instance_id"
  state_root=$(jq -r '.stateRoot // ""' "$request")
  [[ $state_root == /* ]] || { json failed 'absolute request stateRoot is required'; exit 1; }
  state_file="$state_root/instances/$provider/$instance_id.json"
fi
case "$action" in doctor|plan|provision|start|exec|stop|destroy) ;; *) json failed 'unknown action'; exit 2 ;; esac
[[ $(uname -s) == Darwin && $(uname -m) == arm64 ]] || { if [[ $action == doctor || $action == plan ]]; then pending 'macOS runner requires a macOS arm64 host.'; else fail 'unsupported host platform'; fi; }

image_ref=$(jq -r '.providers["macos-tart"].imageRef // ""' "$config")
image_sha=$(jq -r '.providers["macos-tart"].imageSha256 // ""' "$config")
cpus=$(jq -r '.providers["macos-tart"].cpus // ""' "$config")
memory=$(jq -r '.providers["macos-tart"].memoryGiB // ""' "$config")
disk=$(jq -r '.providers["macos-tart"].diskGiB // ""' "$config")
[[ $image_ref =~ ^ghcr\.io/cirruslabs/macos-sequoia-base@sha256:[a-f0-9]{64}$ && ${image_ref##*@sha256:} == "$image_sha" && $cpus =~ ^[1-9][0-9]?$ && $memory =~ ^[1-9][0-9]?$ && $disk =~ ^[2-9][0-9]?$ ]] || { if [[ $action == doctor || $action == plan ]]; then pending 'Immutable macOS 15 image reference/digest and valid resources are required.'; else fail 'invalid pinned macOS image or resource configuration'; fi; }

if [[ $action == doctor || $action == plan ]]; then
  command -v tart >/dev/null || pending 'Install a pinned Tart release before running this provider.'
  for tool in jq openssl shasum tar mktemp seq; do command -v "$tool" >/dev/null || pending "Required host tool missing: $tool"; done
  tart exec --help >/dev/null 2>&1 || pending 'Tart guest-agent exec support is required.'
  tart exec --help 2>&1 | grep -q -- '-i' || pending 'Tart guest-agent stdin support is required.'
  if [[ $action == doctor ]]; then json ready 'macOS arm64 host, Tart guest-agent support, image digest, and resources validated'; else json ready 'macOS 15 arm64 guest can be provisioned from the pinned digest'; fi
  exit 0
fi
command -v tart >/dev/null || { json pending 'Tart is not installed'; exit 0; }
[[ -n ${instance_name:-} ]] || { json failed 'instanceId is required for lifecycle actions'; exit 1; }

owned() {
  need_file "$state_file" && [[ $(jq -r '.provider // ""' "$state_file") == "$provider" && $(jq -r '.instanceId // ""' "$state_file") == "$instance_id" && $(jq -r '.vmName // ""' "$state_file") == "$instance_name" ]]
}

case "$action" in
  provision)
    [[ ! -e $state_file ]] || fail 'tracked instance already exists; use its lifecycle or choose a new instanceId'
    mkdir -p "$(dirname "$state_file")"
    chmod 700 "$(dirname "$state_file")"
    inventory=$(tart list --format json 2>/dev/null) || fail 'Tart VM inventory failed; refusing to risk an unmanaged VM'
    printf '%s' "$inventory" | jq -e 'type=="array"' >/dev/null || fail 'Tart VM inventory was not valid JSON'
    if printf '%s' "$inventory" | jq -e --arg n "$instance_name" 'any(.[]; .Name==$n)' >/dev/null; then fail 'matching Tart VM exists but is not owned by this runner'; fi
    jq -cn --arg id "$instance_id" --arg vm "$instance_name" --arg sha "$image_sha" '{schemaVersion:1,provider:"macos-tart",instanceId:$id,vmName:$vm,imageSha256:$sha}' > "$state_file.tmp"
    chmod 600 "$state_file.tmp"
    mv "$state_file.tmp" "$state_file"
    if ! tart clone "$image_ref" "$instance_name" >/dev/null 2>&1; then fail 'Tart clone failed; ownership record retained so cleanup can inspect a partial clone'; fi
    if ! tart set "$instance_name" --cpu "$cpus" --memory "$((memory * 1024))" --disk-size "$disk" >/dev/null 2>&1; then fail 'Tart resource configuration failed; ownership record retained for cleanup'; fi
    json completed 'Pinned macOS guest cloned and configured; it has not been started' "$instance_id"
    ;;
  start)
    owned || fail 'instance is not tracked by this runner'
    source_archive=$(jq -r '.sourceArchive // ""' "$request")
    source_sha=$(jq -r '.sourceSha256 // ""' "$request")
    need_file "$source_archive" || fail 'source archive is required to start an isolated job checkout'
    [[ $source_sha =~ ^[a-f0-9]{64}$ ]] || fail 'sourceSha256 is required'
    [[ $(shasum -a 256 "$source_archive" | awk '{print $1}') == "$source_sha" ]] || fail 'source archive SHA-256 mismatch'
    result_dir=$(jq -r '.resultDirectory // ""' "$request")
    [[ $result_dir == /* ]] || fail 'absolute resultDirectory is required for VM process logs'
    mkdir -p "$result_dir"
    chmod 700 "$result_dir"
    (nohup tart run --no-graphics --no-audio --no-clipboard "$instance_name" >"$result_dir/tart.log" 2>&1 < /dev/null & echo $! > "$state_file.pid")
    chmod 600 "$state_file.pid"
    booted=false
    for _ in $(seq 1 180); do
      if tart get --format json "$instance_name" 2>/dev/null | jq -e '.Running == true' >/dev/null; then
      if tart exec "$instance_name" /usr/bin/sw_vers -productVersion >/dev/null 2>&1; then booted=true; break; fi
      fi
      sleep 2
    done
    [[ $booted == true ]] || fail 'Tart guest did not boot with its guest agent before the 6 minute deadline'
    product=$(tart exec "$instance_name" /usr/bin/sw_vers -productVersion 2>/dev/null | tr -d '\r')
    [[ $product == 15.* ]] || fail "pinned Tart guest is not macOS 15 (reported $product)"
    guest_arch=$(tart exec "$instance_name" /usr/bin/uname -m 2>/dev/null | tr -d '\r')
    [[ $guest_arch == arm64 ]] || fail "Tart guest architecture is not arm64 (reported $guest_arch)"
    # Guest-agent RPC is host-local and used for bootstrap; the random password
    # goes only through stdin and never appears in arguments, output, or config.
    random_password=$(openssl rand -hex 32)
    if ! printf '%s\n' "$random_password" | tart exec -i "$instance_name" /bin/sh -c 'IFS= read -r password; exec /usr/bin/sudo -n /usr/bin/dscl . -passwd /Users/admin "$password"' >/dev/null 2>&1; then unset random_password; fail 'could not rotate the image bootstrap account password'; fi
    unset random_password
    if ! tart exec "$instance_name" /usr/bin/sudo -n /usr/sbin/systemsetup -setremotelogin off >/dev/null 2>&1; then fail 'could not disable SSH remote login before jobs'; fi
    if ! tart exec -i "$instance_name" /usr/bin/tee /tmp/neri-source.tar < "$source_archive" >/dev/null 2>&1; then fail 'could not transfer the source archive through guest-agent stdin'; fi
    if ! tart exec "$instance_name" /usr/bin/sudo -n /usr/bin/install -o root -g wheel -m 0444 /tmp/neri-source.tar /private/var/tmp/neri-source.tar >/dev/null 2>&1; then fail 'could not protect source archive inside guest'; fi
    guest_sha=$(tart exec "$instance_name" /usr/bin/sudo -n /usr/bin/shasum -a 256 /private/var/tmp/neri-source.tar 2>/dev/null | awk '{print $1}')
    [[ $guest_sha == "$source_sha" ]] || fail 'guest source archive SHA-256 mismatch'
    if ! tart exec "$instance_name" /bin/rm -f /tmp/neri-source.tar >/dev/null 2>&1; then fail 'could not remove temporary source transfer'; fi
    jq --arg sha "$source_sha" --arg os "$product" --arg arch "$guest_arch" '. + {sourceArchiveSha256:$sha,guestOS:$os,guestArchitecture:$arch}' "$state_file" > "$state_file.tmp" && mv "$state_file.tmp" "$state_file"
    json completed 'macOS 15 arm64 guest started, bootstrap credentials rotated, SSH disabled, immutable source snapshot ready' "$instance_id"
    ;;
  exec)
    owned || fail 'instance is not tracked by this runner'
    inventory=$(tart list --format json 2>/dev/null) || fail 'Tart VM inventory failed before exec'
    printf '%s' "$inventory" | jq -e --arg n "$instance_name" 'any(.[]; .Name==$n and .Running==true)' >/dev/null || fail 'tracked Tart guest is not running'
    jq -e '.argv | type=="array" and length>0 and all(.[]; type=="string")' "$request" >/dev/null || fail 'exec argv must be a non-empty string array of strings'
    job_id=$(jq -r '.jobId // ""' "$request")
    valid_id "$job_id" || fail 'a safe jobId is required for a fresh per-job checkout'
    argv=()
    while IFS= read -r -d '' arg; do argv+=("$arg"); done < <(jq -jer '.argv[] | . + "\u0000"' "$request")
    result_dir=$(jq -r '.resultDirectory // ""' "$request")
    [[ $result_dir == /* ]] || fail 'absolute resultDirectory is required for exec'
    mkdir -p "$result_dir"
    out=$(mktemp "$result_dir/stdout.XXXXXXXX")
    err=$(mktemp "$result_dir/stderr.XXXXXXXX")
    checkout='set -eu; base=/Users/admin/neri-jobs; work="$base/$1"; mkdir -p "$base"; if [ ! -f "$work/.neri-source-ready" ]; then tmp=$(mktemp -d "$base/.$1.XXXXXXXX"); /usr/bin/sudo -n /usr/bin/tar -xf /private/var/tmp/neri-source.tar -C "$tmp"; /usr/bin/sudo -n /usr/sbin/chown -R admin:staff "$tmp"; touch "$tmp/.neri-source-ready"; mv "$tmp" "$work"; fi; cd "$work"; shift; exec "$@"'
    if tart exec "$instance_name" /usr/bin/env RUNNER_TEMP=/tmp/neri-runner-temp PATH=/usr/local/bin:/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin /bin/bash -c "$checkout" neri-ci "$job_id" "${argv[@]}" >"$out" 2>"$err"; then code=0; else code=$?; fi
    ev=$(mktemp "$result_dir/evidence.XXXXXXXX")
    jq -cn --arg provider "$provider" --arg instance "$instance_id" --arg job "$job_id" --arg sha "$(jq -r '.imageSha256' "$state_file")" --arg sourceSha "$(jq -r '.sourceArchiveSha256 // ""' "$state_file")" --arg os "$(jq -r '.guestOS // ""' "$state_file")" --arg arch "$(jq -r '.guestArchitecture // ""' "$state_file")" --arg exit "$code" '{schemaVersion:1,provider:$provider,instanceId:$instance,jobId:$job,guestImageSha256:$sha,sourceArchiveSha256:$sourceSha,guestOS:$os,guestArchitecture:$arch,exitCode:($exit|tonumber)}' > "$ev"
    json completed 'guest command completed' "$instance_id" "$code" "$out" "$err" "$ev"
    ;;
  stop)
    if [[ ! -e $state_file ]]; then json completed 'no tracked Tart guest exists; cleanup is already complete' "$instance_id"; exit 0; fi
    owned || fail 'instance is not tracked by this runner'
    inventory=$(tart list --format json 2>/dev/null) || fail 'Tart inventory failed; refusing cleanup'
    if printf '%s' "$inventory" | jq -e --arg n "$instance_name" 'any(.[]; .Name==$n and .Running==true)' >/dev/null; then tart stop "$instance_name" >/dev/null 2>&1 || fail 'Tart stop failed'; fi
    json completed 'tracked Tart guest stopped' "$instance_id"
    ;;
  destroy)
    if [[ ! -e $state_file ]]; then json completed 'no tracked Tart guest exists; cleanup is already complete' "$instance_id"; exit 0; fi
    owned || fail 'instance is not tracked by this runner'
    inventory=$(tart list --format json 2>/dev/null) || fail 'Tart inventory failed; refusing cleanup'
    if printf '%s' "$inventory" | jq -e --arg n "$instance_name" 'any(.[]; .Name==$n)' >/dev/null; then
      tart stop "$instance_name" >/dev/null 2>&1 || true
      tart delete "$instance_name" >/dev/null 2>&1 || fail 'Tart delete failed'
    fi
    rm -f "$state_file" "$state_file.pid"
    json completed 'tracked Tart guest destroyed' "$instance_id"
    ;;
esac
