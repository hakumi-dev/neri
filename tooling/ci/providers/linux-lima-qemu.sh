#!/usr/bin/env bash
# Neri-managed Lima/QEMU guest provider. It never addresses an untracked Lima VM.
set -euo pipefail
umask 077

provider=linux-lima-qemu
asset_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
request=
action=${1:-}
shift || true
if [[ ${1:-} == --request-json && $# -eq 2 ]]; then request=$2; else
  printf '%s\n' 'usage: linux-lima-qemu.sh <doctor|plan|provision|start|exec|stop|destroy> --request-json FILE' >&2
  exit 2
fi

json() { jq -cn --arg p "$provider" --arg a "$action" --arg s "$1" --arg r "${2:-}" --arg i "${3:-}" --arg op "${5:-}" --arg ep "${6:-}" --arg ev "${7:-}" --argjson x "${4:-null}" '{schemaVersion:1,provider:$p,action:$a,status:$s} + (if $r == "" then {} else {reason:$r} end) + (if $i == "" then {} else {instanceId:$i} end) + (if $x == null then {} else {exitCode:$x} end) + (if $op == "" then {} else {stdoutPath:$op} end) + (if $ep == "" then {} else {stderrPath:$ep} end) + (if $ev == "" then {} else {evidencePath:$ev} end)'; }
fail() { json failed "$1" "${instance_name:-}"; exit 1; }
pending() { json pending "$1" "${instance_name:-}"; exit 0; }
need_file() { [[ -f $1 && ! -L $1 ]]; }
valid_id() { [[ $1 =~ ^[a-z0-9][a-z0-9-]{0,70}$ ]]; }

need_file "$request" || { json failed 'request JSON file missing'; exit 1; }
command -v jq >/dev/null || { printf '%s\n' '{"schemaVersion":1,"provider":"linux-lima-qemu","action":"unknown","status":"failed","reason":"jq is required"}'; exit 1; }
[[ $(jq -r '.schemaVersion // 0' "$request") == 1 ]] || { json failed 'request schemaVersion must be 1'; exit 1; }
[[ $(jq -r '.provider // ""' "$request") == "$provider" ]] || { json failed 'request provider mismatch'; exit 1; }
config=$(jq -r '.configPath // ""' "$request")
need_file "$config" || { json failed 'local-ci.json missing'; exit 1; }
[[ $(jq -r '.schemaVersion // 0' "$config") == 1 ]] || { json failed 'config schemaVersion must be 1'; exit 1; }
instance_id=$(jq -r '.instanceId // ""' "$request")
job_id=$(jq -r '.jobId // ""' "$request")
if [[ -n $instance_id ]]; then
  valid_id "$instance_id" || { json failed 'unsafe instanceId'; exit 1; }
  instance_name="neri-ci-$instance_id"
  state_root=$(jq -r '.stateRoot // ""' "$request")
  [[ $state_root == /* ]] || { json failed 'absolute request stateRoot is required'; exit 1; }
  state_root="$state_root/instances/$provider"
  state_file="$state_root/$instance_id.json"
fi

host_os=$(uname -s)
host_arch=$(uname -m)
case "$action" in
  doctor|plan|provision|start|exec|stop|destroy) ;;
  *) json failed 'unknown action'; exit 2 ;;
esac

if [[ $action == doctor || $action == plan ]]; then
  [[ $host_os == Darwin && $host_arch == arm64 ]] || pending 'Linux x86_64 virtualization requires the supported macOS arm64 host.'
  command -v limactl >/dev/null || pending 'Install a pinned Lima release before running this provider.'
  command -v qemu-system-x86_64 >/dev/null || pending 'Install a pinned QEMU release with x86_64 TCG support.'
  for tool in jq shasum sed tar mktemp; do command -v "$tool" >/dev/null || pending "Required host tool missing: $tool"; done
  qemu-system-x86_64 -accel help 2>&1 | grep -qx 'tcg' || pending 'QEMU x86_64 TCG acceleration is unavailable.'
  image_url=$(jq -r '.providers["linux-lima-qemu"].imageUrl // ""' "$config")
  image_sha=$(jq -r '.providers["linux-lima-qemu"].imageSha256 // ""' "$config")
  apt_snapshot=$(jq -r '.providers["linux-lima-qemu"].aptSnapshot // ""' "$config")
  cpus=$(jq -r '.providers["linux-lima-qemu"].cpus // ""' "$config")
  memory=$(jq -r '.providers["linux-lima-qemu"].memoryGiB // ""' "$config")
  disk=$(jq -r '.providers["linux-lima-qemu"].diskGiB // ""' "$config")
  [[ $image_url == https://cloud-images.ubuntu.com/releases/server/24.04/release-20260926/ubuntu-24.04-server-cloudimg-amd64.img && $image_sha =~ ^[a-f0-9]{64}$ && $apt_snapshot =~ ^[0-9]{8}T[0-9]{6}Z$ && $cpus =~ ^[1-9][0-9]?$ && $memory =~ ^[1-9][0-9]?$ && $disk =~ ^[2-9][0-9]?$ ]] || pending 'Pinned Ubuntu image URL, SHA-256, apt snapshot, and valid resource values are required.'
  if [[ $action == doctor ]]; then json ready 'host/provider prerequisites and pinned inputs validated'; else json ready 'Ubuntu 24.04 x86_64 QEMU/TCG guest can be provisioned'; fi
  exit 0
fi

[[ $host_os == Darwin && $host_arch == arm64 ]] || { json failed 'unsupported host platform'; exit 1; }
command -v limactl >/dev/null || { json pending 'Lima is not installed'; exit 0; }
command -v qemu-system-x86_64 >/dev/null || { json pending 'QEMU x86_64 is not installed'; exit 0; }
qemu-system-x86_64 -accel help 2>&1 | grep -qx 'tcg' || { json pending 'QEMU TCG acceleration is unavailable'; exit 0; }
  [[ -n ${instance_name:-} ]] || { json failed 'instanceId is required for lifecycle actions'; exit 1; }

case "$action" in
  provision)
    [[ ! -e $state_file ]] || fail 'tracked instance already exists; use its lifecycle or choose a new instanceId'
    mkdir -p "$state_root"
    chmod 700 "$state_root"
    if limactl list --format '{{.Name}}' 2>/dev/null | grep -Fxq "$instance_name"; then fail 'matching Lima instance exists but is not owned by this runner'; fi
    image_url=$(jq -r '.providers["linux-lima-qemu"].imageUrl' "$config")
    image_sha=$(jq -r '.providers["linux-lima-qemu"].imageSha256' "$config")
    apt_snapshot=$(jq -r '.providers["linux-lima-qemu"].aptSnapshot' "$config")
    cpus=$(jq -r '.providers["linux-lima-qemu"].cpus' "$config")
    memory=$(jq -r '.providers["linux-lima-qemu"].memoryGiB' "$config")
    disk=$(jq -r '.providers["linux-lima-qemu"].diskGiB' "$config")
    [[ $image_url == https://cloud-images.ubuntu.com/releases/server/24.04/release-20260926/ubuntu-24.04-server-cloudimg-amd64.img && $image_sha =~ ^[a-f0-9]{64}$ && $apt_snapshot =~ ^[0-9]{8}T[0-9]{6}Z$ ]] || fail 'invalid pinned Ubuntu inputs'
    [[ $cpus =~ ^[1-9][0-9]?$ && $memory =~ ^[1-9][0-9]?$ && $disk =~ ^[2-9][0-9]?$ ]] || fail 'invalid guest resource configuration'
    template="$asset_root/lima-ubuntu.yaml.in"
    need_file "$template" || fail 'Lima template missing'
    tmp=$(mktemp "${TMPDIR:-/tmp}/neri-lima-template.XXXXXXXX")
    trap 'rm -f "$tmp"' EXIT
    sed -e "s|@UBUNTU_IMAGE_URL@|$image_url|g" -e "s|@UBUNTU_IMAGE_SHA256@|$image_sha|g" -e "s|@APT_SNAPSHOT@|$apt_snapshot|g" -e "s/^cpus: .*/cpus: $cpus/" -e "s/^memory: .*/memory: \"${memory}GiB\"/" -e "s/^disk: .*/disk: \"${disk}GiB\"/" "$template" > "$tmp"
    jq -cn --arg id "$instance_id" --arg vm "$instance_name" --arg imageSha256 "$image_sha" '{schemaVersion:1,provider:"linux-lima-qemu",instanceId:$id,vmName:$vm,imageSha256:$imageSha256}' > "$state_file.tmp"
    chmod 600 "$state_file.tmp"
    mv "$state_file.tmp" "$state_file"
    if ! limactl create --tty=false --name "$instance_name" "$tmp" >/dev/null 2>&1; then fail 'Lima create failed; ownership record retained so cleanup can inspect the partial instance'; fi
    json completed 'Lima guest definition created; no VM has been started' "$instance_id"
    ;;
  start)
    need_file "$state_file" && [[ $(jq -r '.provider // ""' "$state_file") == "$provider" && $(jq -r '.instanceId // ""' "$state_file") == "$instance_id" && $(jq -r '.vmName // ""' "$state_file") == "$instance_name" ]] || fail 'instance is not tracked by this runner'
    source_archive=$(jq -r '.sourceArchive // ""' "$request")
    source_sha=$(jq -r '.sourceSha256 // ""' "$request")
    need_file "$source_archive" || fail 'source archive is required to start an isolated job checkout'
    [[ $source_sha =~ ^[a-f0-9]{64}$ ]] || fail 'sourceSha256 is required'
    actual_sha=$(shasum -a 256 "$source_archive" | awk '{print $1}')
    [[ $actual_sha == "$source_sha" ]] || fail 'source archive SHA-256 mismatch'
    if ! limactl start --tty=false "$instance_name" >/dev/null 2>&1; then fail 'Lima guest failed to start'; fi
    if ! limactl copy "$source_archive" "$instance_name:/tmp/neri-source.tar" >/dev/null 2>&1; then fail 'could not copy source archive into guest'; fi
    if ! limactl shell "$instance_name" -- bash -c 'set -eu; sudo install -o root -g root -m 0444 /tmp/neri-source.tar /var/tmp/neri-source.tar; rm -f /tmp/neri-source.tar' >/dev/null 2>&1; then fail 'could not protect source archive inside guest'; fi
    guest_sha=$(limactl shell "$instance_name" -- sudo sha256sum /var/tmp/neri-source.tar 2>/dev/null | awk '{print $1}')
    [[ $guest_sha == "$source_sha" ]] || fail 'guest source archive SHA-256 mismatch'
    jq --arg sha "$source_sha" '. + {sourceArchiveSha256:$sha}' "$state_file" > "$state_file.tmp" && mv "$state_file.tmp" "$state_file"
    jq --arg os 'Ubuntu 24.04 x86_64' '. + {guestOS:$os}' "$state_file" > "$state_file.tmp" && mv "$state_file.tmp" "$state_file"
    json completed 'Ubuntu 24.04 x86_64 guest started with the immutable source snapshot ready' "$instance_id"
    ;;
  exec)
    need_file "$state_file" && [[ $(jq -r '.provider // ""' "$state_file") == "$provider" && $(jq -r '.instanceId // ""' "$state_file") == "$instance_id" && $(jq -r '.vmName // ""' "$state_file") == "$instance_name" ]] || fail 'instance is not tracked by this runner'
    [[ $(limactl list --format '{{.Name}} {{.Status}}' 2>/dev/null | awk -v n="$instance_name" '$1==n {print $2}') == Running ]] || fail 'tracked guest is not running'
    jq -e '.argv | type=="array" and length>0 and all(.[]; type=="string")' "$request" >/dev/null || fail 'exec argv must be a non-empty string array of strings'
    job_id=$(jq -r '.jobId // ""' "$request")
    valid_id "$job_id" || fail 'a safe jobId is required for a fresh per-job checkout'
    argv=()
    while IFS= read -r -d '' arg; do argv+=("$arg"); done < <(jq -jer '.argv[] | . + "\u0000"' "$request")
    result_dir=$(jq -r '.resultDirectory // ""' "$request")
    [[ $result_dir == /* ]] || fail 'absolute resultDirectory is required for exec'
    mkdir -p "$result_dir"
    chmod 700 "$result_dir"
    out=$(mktemp "$result_dir/stdout.XXXXXXXX")
    err=$(mktemp "$result_dir/stderr.XXXXXXXX")
    setup_checkout='set -eu; base=/tmp/neri-jobs; work="$base/$1"; mkdir -p "$base"; if [ ! -f "$work/.neri-source-ready" ]; then tmp=$(mktemp -d "$base/.$1.XXXXXXXX"); sudo tar -xf /var/tmp/neri-source.tar -C "$tmp"; sudo chown -R "$(id -u):$(id -g)" "$tmp"; touch "$tmp/.neri-source-ready"; mv "$tmp" "$work"; fi; cd "$work"; shift; exec "$@"'
    if limactl shell "$instance_name" -- env RUNNER_TEMP=/tmp/neri-runner-temp PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin bash -c "$setup_checkout" neri-ci "$job_id" "${argv[@]}" >"$out" 2>"$err"; then code=0; else code=$?; fi
    ev=$(mktemp "$result_dir/evidence.XXXXXXXX")
    jq -cn --arg provider "$provider" --arg instance "$instance_id" --arg job "$job_id" --arg sha "$(jq -r '.imageSha256' "$state_file")" --arg sourceSha "$(jq -r '.sourceArchiveSha256 // ""' "$state_file")" --arg os "$(jq -r '.guestOS // ""' "$state_file")" --arg exit "$code" '{schemaVersion:1,provider:$provider,instanceId:$instance,jobId:$job,guestImageSha256:$sha,sourceArchiveSha256:$sourceSha,guestOS:$os,exitCode:($exit|tonumber)}' > "$ev"
    json completed 'guest command completed' "$instance_id" "$code" "$out" "$err" "$ev"
    ;;
  stop)
    if [[ ! -e $state_file ]]; then json completed 'no tracked Lima guest exists; cleanup is already complete' "$instance_id"; exit 0; fi
    need_file "$state_file" && [[ $(jq -r '.provider // ""' "$state_file") == "$provider" && $(jq -r '.instanceId // ""' "$state_file") == "$instance_id" && $(jq -r '.vmName // ""' "$state_file") == "$instance_name" ]] || fail 'instance is not tracked by this runner'
    inventory=$(limactl list --format '{{.Name}} {{.Status}}' 2>/dev/null) || fail 'Lima inventory failed; refusing cleanup'
    status=$(printf '%s\n' "$inventory" | awk -v n="$instance_name" '$1==n {print $2}')
    if [[ -n $status && $status != Stopped ]]; then limactl stop "$instance_name" >/dev/null 2>&1 || fail 'Lima stop failed'; fi
    json completed 'tracked Lima guest stopped' "$instance_id"
    ;;
  destroy)
    if [[ ! -e $state_file ]]; then json completed 'no tracked Lima guest exists; cleanup is already complete' "$instance_id"; exit 0; fi
    need_file "$state_file" && [[ $(jq -r '.provider // ""' "$state_file") == "$provider" && $(jq -r '.instanceId // ""' "$state_file") == "$instance_id" && $(jq -r '.vmName // ""' "$state_file") == "$instance_name" ]] || fail 'instance is not tracked by this runner'
    inventory=$(limactl list --format '{{.Name}}' 2>/dev/null) || fail 'Lima inventory failed; refusing cleanup'
    if printf '%s\n' "$inventory" | grep -Fxq "$instance_name"; then
      limactl stop "$instance_name" >/dev/null 2>&1 || true
      limactl delete --force "$instance_name" >/dev/null 2>&1 || fail 'Lima delete failed'
    fi
    rm -f "$state_file"
    json completed 'tracked Lima guest destroyed' "$instance_id"
    ;;
esac
