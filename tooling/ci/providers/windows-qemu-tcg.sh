#!/usr/bin/env bash
set -euo pipefail
umask 077
provider=windows-qemu-tcg
asset_root=$(cd "$(dirname "$0")/.." && pwd)
action=${1:-}
[[ $# -gt 0 ]] && shift
if [[ $action != fingerprint-layout ]]; then
  [[ ${1:-} == --request-json && $# -eq 2 ]] || exit 2
  request=$2
fi
reply() {
  jq -cn --arg a "$action" --arg s "$1" --arg r "$2" --arg i "$instance_id" --arg o "$4" --arg e "$5" --arg v "$6" --argjson c "$3" \
    '{schemaVersion:1,provider:"windows-qemu-tcg",action:$a,status:$s}
    + (if $r=="" then {} else {reason:$r} end)
    + (if $i=="" then {} else {instanceId:$i} end)
    + (if $c==null then {} else {exitCode:$c} end)
    + (if $o=="" then {} else {stdoutPath:$o} end)
    + (if $e=="" then {} else {stderrPath:$e} end)
    + (if $v=="" then {} else {evidencePath:$v} end)'
}
failed() { reply failed "$1" null "" "" ""; exit 1; }
pending() { reply pending "$1" null "" "" ""; exit 0; }
done_reply() { reply completed "$1" "$2" "$3" "$4" "$5"; }
file() { [[ -f $1 && ! -L $1 ]]; }
sha_file() { shasum -a 256 "$1" | awk '{print tolower($1)}'; }
lower() { printf '%s' "$1" | tr '[:upper:]' '[:lower:]'; }
qemu_path() { printf '%s' "$1" | sed 's/,/,,/g'; }
sha_tree() {
  (cd "$1"; find . -type f -print0 | LC_ALL=C sort -z | while IFS= read -r -d '' item; do
    printf '%s\0%s\0' "$(sha_file "$item")" "$item"
  done) | shasum -a 256 | awk '{print tolower($1)}'
}
setting() { jq -r --arg k "$1" '.providers["windows-qemu-tcg"][$k] // empty' "$config"; }
input() { jq -r --arg n "$1" --arg k "$2" '.providers["windows-qemu-tcg"].requiredInputs[$n][$k] // empty' "$config"; }
preflight() {
  [[ $(uname -s) == Darwin && $(uname -m) == arm64 ]] || pending 'Windows x64 TCG requires a macOS arm64 host.'
  for tool in hdiutil qemu-img ssh scp ssh-keygen shasum timeout openssl jq lsof; do
    command -v "$tool" >/dev/null || pending "Required host tool missing: $tool"
  done
  [[ $(setting guestOs) == 'Windows Server 2025 Evaluation' && $(setting requiredBuild) == 26100 && $(setting architecture) == x86_64 && $(setting acceleration) == tcg ]] || pending 'Windows Server 2025 x64 TCG configuration is required.'
  image_index=$(setting imageIndex)
  [[ $image_index =~ ^[1-9][0-9]*$ ]] || pending 'Choose an explicit imageIndex from the pinned Windows ISO.'
  iso=$(setting isoPath)
  iso_sha=$(setting isoSha256)
  [[ $iso == /* && $iso_sha =~ ^[a-fA-F0-9]{64}$ ]] && file "$iso" || pending 'Pinned Windows ISO path and SHA-256 required.'
  cpus=$(setting cpus)
  memory=$(setting memoryGiB)
  disk=$(setting diskGiB)
  [[ $cpus =~ ^[1-9][0-9]?$ && $memory =~ ^[1-9][0-9]?$ && $disk =~ ^[1-9][0-9]*$ ]] || pending 'Invalid Windows VM resource configuration.'
  for name in qemu ovmfCode ovmfVarsTemplate powershell7Msi node22Installer visualStudioBuildToolsLayout win32OpenSshPackage llvmArchive; do
    path=$(input "$name" path)
    hash=$(input "$name" sha256)
    [[ $path == /* && $hash =~ ^[a-fA-F0-9]{64}$ ]] || pending "Pinned path and SHA-256 required for $name."
    if [[ $name == visualStudioBuildToolsLayout ]]; then
      [[ -d $path && ! -L $path ]] || pending "Pinned directory missing: $name."
      [[ -z $(find "$path" -type l -print -quit) ]] || pending 'Visual Studio layout contains symbolic links.'
    else
      file "$path" || pending "Pinned file missing: $name."
    fi
  done
  [[ $(input llvmArchive version) == 22.1.8 ]] || pending 'LLVM archive version must be 22.1.8.'
  [[ $(lower "$(input powershell7Msi path)") == *.msi && $(lower "$(input node22Installer path)") == *.msi ]] || pending 'PowerShell 7 and Node 22 offline installers must be MSI packages.'
  [[ $(lower "$(input win32OpenSshPackage path)") == *.zip && $(lower "$(input llvmArchive path)") == *.tar.xz ]] || pending 'OpenSSH ZIP and LLVM tar.xz offline packages are required.'
  qemu=$(input qemu path)
  [[ -x $qemu ]] || pending 'Pinned QEMU is not executable.'
  "$qemu" -accel help 2>&1 | grep -qx tcg || pending 'Pinned QEMU lacks TCG.'
  for name in windows-bootstrap.ps1 windows-execute.ps1 Autounattend.xml.in; do
    file "$asset_root/guest/$name" || pending "Missing guest asset: $name"
  done
}
verify_pins() {
  [[ $(sha_file "$iso") == "$(lower "$iso_sha")" ]] || pending 'Windows ISO SHA-256 mismatch.'
  for name in qemu ovmfCode ovmfVarsTemplate powershell7Msi node22Installer visualStudioBuildToolsLayout win32OpenSshPackage llvmArchive; do
    path=$(input "$name" path)
    hash=$(input "$name" sha256)
    if [[ $name == visualStudioBuildToolsLayout ]]; then actual=$(sha_tree "$path"); else actual=$(sha_file "$path"); fi
    [[ $actual == "$(lower "$hash")" ]] || pending "Pinned SHA-256 mismatch: $name."
  done
}
ssh_guest() {
  timeout "$seconds" ssh -o BatchMode=yes -o ConnectTimeout=5 -o StrictHostKeyChecking=accept-new -o UserKnownHostsFile="$instance_dir/known_hosts" -i "$instance_dir/ssh_key" -p "$port" Administrator@127.0.0.1 "$@"
}
scp_guest() {
  timeout "$seconds" scp -o BatchMode=yes -o ConnectTimeout=5 -o StrictHostKeyChecking=accept-new -o UserKnownHostsFile="$instance_dir/known_hosts" -i "$instance_dir/ssh_key" -P "$port" "$1" "Administrator@127.0.0.1:$2"
}
owned() {
  file "$instance_dir/owned.json" || failed 'Instance is not tracked by this provider.'
  [[ $(jq -r '.instanceId' "$instance_dir/owned.json") == "$instance_id" && $(jq -r '.provider' "$instance_dir/owned.json") == "$provider" ]] || failed 'Instance ownership mismatch.'
}

if [[ $action == fingerprint-layout ]]; then
  path=${1:-}
  [[ $# -eq 1 && $path == /* && -d $path && ! -L $path ]] || { printf '%s\n' 'Expected an absolute Visual Studio layout directory.' >&2; exit 2; }
  [[ -z $(find "$path" -type l -print -quit) ]] || { printf '%s\n' 'Layout must not contain symbolic links.' >&2; exit 2; }
  sha_tree "$path"
  exit 0
fi
command -v jq >/dev/null || exit 2

instance_id=
file "$request" || failed 'Request JSON file missing.'
[[ $(jq -r '.schemaVersion // 0' "$request") == 1 && $(jq -r '.provider // ""' "$request") == "$provider" ]] || failed 'Request schema/provider mismatch.'
case "$action" in doctor|plan|provision|start|exec|stop|destroy) ;; *) failed 'Unknown action.' ;; esac
config=$(jq -r '.configPath // ""' "$request")
file "$config" || failed 'Local CI config missing.'
[[ $(jq -r '.schemaVersion // 0' "$config") == 1 ]] || failed 'Config schema mismatch.'
state_root=$(jq -r '.stateRoot // ""' "$request")
[[ $state_root == /* ]] || failed 'Absolute stateRoot required.'
instance_id=$(jq -r '.instanceId // ""' "$request")
[[ -z $instance_id || $instance_id =~ ^[a-z0-9][a-z0-9-]{0,70}$ ]] || failed 'Unsafe instanceId.'
job_id=$(jq -r '.jobId // ""' "$request")
[[ -z $job_id || $job_id =~ ^[a-z0-9][a-z0-9-]{0,70}$ ]] || failed 'Unsafe jobId.'
seconds=$(jq -r '.timeoutSeconds // 0' "$request")
[[ $seconds =~ ^[1-9][0-9]*$ ]] || failed 'Positive timeoutSeconds required.'
result_dir=$(jq -r '.resultDirectory // ""' "$request")
[[ -z $result_dir || $result_dir == /* ]] || failed 'Absolute resultDirectory required.'
instance_dir="$state_root/instances/$provider/$instance_id"
if [[ $action == doctor || $action == plan ]]; then
  preflight
  reply ready 'Pinned input metadata and host capabilities are present; digests verify during provision.' null "" "" ""
  exit 0
fi
[[ -n $instance_id ]] || failed 'Instance ID required.'
if [[ $action == provision ]]; then
  preflight
  verify_pins
  [[ ! -e $instance_dir ]] || { owned; done_reply 'Already provisioned.' null "" "" ""; exit 0; }
  mkdir -m 700 -p "$instance_dir"
  trap 'rm -rf "$instance_dir"' ERR
  jq -cn --arg p "$provider" --arg i "$instance_id" '{schemaVersion:1,provider:$p,instanceId:$i}' > "$instance_dir/owned.json"
  qemu-img create -f qcow2 "$instance_dir/disk.qcow2" "$disk"G >/dev/null
  cp "$iso" "$instance_dir/install.iso"
  cp "$(input ovmfCode path)" "$instance_dir/ovmf-code.fd"
  cp "$(input ovmfVarsTemplate path)" "$instance_dir/ovmf-vars.fd"
  ssh-keygen -q -t ed25519 -N '' -f "$instance_dir/ssh_key"
  mkdir "$instance_dir/answer" "$instance_dir/tools"
  password="Neri!$(openssl rand -hex 32)"
  sed -e "s/__IMAGE_INDEX__/$image_index/g" -e "s/__ADMIN_PASSWORD__/$password/g" "$asset_root/guest/Autounattend.xml.in" > "$instance_dir/answer/Autounattend.xml"
  cp "$asset_root/guest/windows-bootstrap.ps1" "$asset_root/guest/windows-execute.ps1" "$instance_dir/answer/"
  cp "$instance_dir/ssh_key.pub" "$instance_dir/answer/authorized_keys.txt"
  cp "$(input win32OpenSshPackage path)" "$instance_dir/answer/OpenSSH-Win64.zip"
  cp "$(input powershell7Msi path)" "$instance_dir/tools/PowerShell7.msi"
  cp "$(input node22Installer path)" "$instance_dir/tools/Node22.msi"
  cp -R "$(input visualStudioBuildToolsLayout path)" "$instance_dir/tools/vs-layout"
  cp "$(input llvmArchive path)" "$instance_dir/tools/llvm.tar.xz"
  hdiutil makehybrid -quiet -o "$instance_dir/answer.iso" "$instance_dir/answer" -iso -joliet -udf -default-volume-name NERI-CI >/dev/null
  hdiutil makehybrid -quiet -o "$instance_dir/tools.iso" "$instance_dir/tools" -iso -joliet -udf -default-volume-name NERI-TOOLS >/dev/null
  trap - ERR
  done_reply 'Pinned guest disk and offline media provisioned; VM not started.' null "" "" ""
  exit 0
fi
if [[ $action == destroy && ! -e $instance_dir && -f $state_root/destroyed/$provider/$instance_id ]]; then
  done_reply 'Tracked Windows VM already destroyed.' null "" "" ""
  exit 0
fi
owned
if file "$instance_dir/ssh-port"; then port=$(cat "$instance_dir/ssh-port"); fi
case "$action" in
  start)
    [[ $(uname -s) == Darwin && $(uname -m) == arm64 ]] || failed 'Unsupported host.'
    qemu=$(input qemu path)
    file "$qemu" && [[ -x $qemu ]] || failed 'Pinned QEMU is unavailable.'
    [[ $(sha_file "$qemu") == "$(lower "$(input qemu sha256)")" ]] || failed 'Pinned QEMU digest changed since provision.'
    cpus=$(setting cpus)
    memory=$(setting memoryGiB)
    [[ ! -e $instance_dir/qemu.pid ]] || failed 'Tracked VM is already started.'
    port=$(jot -r 1 30000 49999)
    attempts=0
    while lsof -nP -iTCP:"$port" -sTCP:LISTEN >/dev/null 2>&1; do
      attempts=$((attempts+1))
      (( attempts < 20 )) || failed 'No loopback SSH port available.'
      port=$(jot -r 1 30000 49999)
    done
    printf '%s\n' "$port" > "$instance_dir/ssh-port"
    "$qemu" -machine q35,accel=tcg -cpu max -smp "$cpus" -m "$((memory*1024))" \
      -drive "if=pflash,format=raw,readonly=on,file=$(qemu_path "$instance_dir/ovmf-code.fd")" \
      -drive "if=pflash,format=raw,file=$(qemu_path "$instance_dir/ovmf-vars.fd")" \
      -device ich9-ahci,id=sata \
      -drive "if=none,id=hd0,format=qcow2,file=$(qemu_path "$instance_dir/disk.qcow2")" -device ide-hd,drive=hd0,bus=sata.0 \
      -drive "if=none,id=cd0,media=cdrom,format=raw,readonly=on,file=$(qemu_path "$instance_dir/install.iso")" -device ide-cd,drive=cd0,bus=sata.1,bootindex=1 \
      -drive "if=none,id=cd1,media=cdrom,format=raw,readonly=on,file=$(qemu_path "$instance_dir/answer.iso")" -device ide-cd,drive=cd1,bus=sata.2 \
      -drive "if=none,id=cd2,media=cdrom,format=raw,readonly=on,file=$(qemu_path "$instance_dir/tools.iso")" -device ide-cd,drive=cd2,bus=sata.3 \
      -netdev "user,id=net0,hostfwd=tcp:127.0.0.1:$port-:22" -device e1000,netdev=net0 \
      -display none -serial "file:$instance_dir/serial.log" -monitor none -daemonize -pidfile "$instance_dir/qemu.pid" || failed 'QEMU startup failed.'
    deadline=$(( $(date +%s) + seconds ))
    while (( $(date +%s) < deadline )); do
      if ssh_guest 'if exist C:\CI\bootstrap.ready (exit 0) else (exit 1)' >/dev/null 2>&1; then
        done_reply 'Windows guest ready with SSH key authentication.' null "" "" "$instance_dir/serial.log"
        exit 0
      fi
      if ! kill -0 "$(cat "$instance_dir/qemu.pid")" 2>/dev/null; then failed 'QEMU exited before SSH readiness.'; fi
      sleep 5
    done
    failed 'Windows guest did not reach SSH readiness before deadline.'
    ;;
  exec)
    [[ -n $job_id && -n $result_dir && -n $port ]] || failed 'Exec requires job, result directory and SSH port.'
    archive=$(jq -r '.sourceArchive // ""' "$request")
    source_sha=$(jq -r '.sourceSha256 // ""' "$request")
    file "$archive" && [[ $source_sha =~ ^[a-fA-F0-9]{64}$ ]] || failed 'Source archive and SHA-256 required.'
    [[ $(sha_file "$archive") == "$(lower "$source_sha")" ]] || failed 'Source archive SHA-256 mismatch.'
    phase=$(jq -r '.argv[5] // ""' "$request")
    [[ $phase == prepare || $phase == run ]] || failed 'Expected prepare/run argv.'
    jq -e --arg j "$job_id" --arg p "$phase" '.argv == ["pwsh","-NoProfile","-File","scripts/ci/jobs.ps1","-Action",$p,"-Job",$j]' "$request" >/dev/null || failed 'Unsupported Windows exec argv.'
    mkdir -p "$result_dir"
    out="$result_dir/$phase.stdout.log"
    err="$result_dir/$phase.stderr.log"
    scp_guest "$archive" 'C:/Users/Administrator/neri-source.tar' >"$out" 2>"$err" || failed 'Source upload failed.'
    jq -cn --arg j "$job_id" --arg p "$phase" --arg h "$source_sha" '{schemaVersion:1,jobId:$j,phase:$p,sourceSha256:$h}' > "$instance_dir/guest-request.json"
    scp_guest "$instance_dir/guest-request.json" 'C:/Users/Administrator/neri-request.json' >>"$out" 2>>"$err" || failed 'Guest request upload failed.'
    if ssh_guest 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File C:\CI\windows-execute.ps1 -RequestPath C:\Users\Administrator\neri-request.json' >>"$out" 2>>"$err"; then code=0; else code=$?; fi
    evidence="$result_dir/$phase.serial.log"
    if file "$instance_dir/serial.log"; then cp "$instance_dir/serial.log" "$evidence"; else : > "$evidence"; fi
    done_reply 'Windows guest command finished.' "$code" "$out" "$err" "$evidence"
    ;;
  stop|destroy)
    if file "$instance_dir/qemu.pid"; then
      pid=$(cat "$instance_dir/qemu.pid")
      [[ $pid =~ ^[0-9]+$ ]] || failed 'Corrupt QEMU PID.'
      if kill -0 "$pid" 2>/dev/null; then
        ps -p "$pid" -o command= | grep -Fq -- "$instance_dir/disk.qcow2" || failed 'Tracked PID no longer belongs to this QEMU instance.'
      fi
      kill "$pid" 2>/dev/null || true
      deadline=$(( $(date +%s) + seconds ))
      while kill -0 "$pid" 2>/dev/null && (( $(date +%s) < deadline )); do sleep 1; done
      if kill -0 "$pid" 2>/dev/null; then kill -9 "$pid" 2>/dev/null || failed 'QEMU did not stop.'; fi
      rm -f "$instance_dir/qemu.pid"
    fi
    if [[ $action == destroy ]]; then
      mkdir -p "$state_root/destroyed/$provider"
      : > "$state_root/destroyed/$provider/$instance_id"
      rm -rf "$instance_dir"
    fi
    done_reply "Tracked Windows VM $action complete." null "" "" ""
    ;;
esac
