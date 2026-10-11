$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ciVolume = Get-Volume | Where-Object FileSystemLabel -eq 'NERI-CI' | Select-Object -First 1
if (-not $ciVolume -or -not $ciVolume.DriveLetter) { throw 'NERI-CI answer media is unavailable.' }
$media = "$($ciVolume.DriveLetter):"
$root = 'C:\CI'
New-Item -ItemType Directory -Force -Path $root | Out-Null
Copy-Item -LiteralPath "$media\windows-execute.ps1" -Destination "$root\windows-execute.ps1"

$package = Join-Path $media 'OpenSSH-Win64.zip'
if (-not (Test-Path -LiteralPath $package)) { throw 'Pinned OpenSSH package is unavailable.' }
Expand-Archive -LiteralPath $package -DestinationPath "$root\OpenSSH" -Force
$install = Get-ChildItem -LiteralPath "$root\OpenSSH" -Recurse -Filter install-sshd.ps1 -File | Select-Object -First 1
if (-not $install) { throw 'Pinned OpenSSH package does not contain install-sshd.ps1.' }
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $install.FullName
if ($LASTEXITCODE -ne 0) { throw 'OpenSSH service installation failed.' }

$key = (Get-Content -LiteralPath "$media\authorized_keys.txt" -Raw).Trim()
if ($key -notmatch '^ssh-ed25519 [A-Za-z0-9+/=]+( .*)?$') { throw 'Invalid bootstrap SSH public key.' }
$sshDirectory = 'C:\ProgramData\ssh'
New-Item -ItemType Directory -Force -Path $sshDirectory | Out-Null
$authorized = Join-Path $sshDirectory 'administrators_authorized_keys'
[IO.File]::WriteAllText($authorized, $key + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
& icacls.exe $authorized /inheritance:r /grant 'Administrators:F' 'SYSTEM:F' | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Cannot restrict authorized key permissions.' }
$configuration = Join-Path $sshDirectory 'sshd_config'
if (-not (Test-Path -LiteralPath $configuration)) {
    $defaultConfiguration = Join-Path $install.Directory.FullName 'sshd_config_default'
    if (-not (Test-Path -LiteralPath $defaultConfiguration)) { throw 'Pinned OpenSSH package has no default sshd_config.' }
    Copy-Item -LiteralPath $defaultConfiguration -Destination $configuration
}
$current = Get-Content -LiteralPath $configuration -Raw
$current = [Regex]::Replace($current, '(?m)^\s*#?\s*(PasswordAuthentication|PubkeyAuthentication)\s+.*$', '')
$current = 'PasswordAuthentication no' + [Environment]::NewLine + 'PubkeyAuthentication yes' + [Environment]::NewLine + $current
[IO.File]::WriteAllText($configuration, $current, [Text.UTF8Encoding]::new($false))
Set-Service -Name sshd -StartupType Automatic
Start-Service -Name sshd
if (-not (Get-NetFirewallRule -DisplayName 'NERI CI SSH' -ErrorAction SilentlyContinue)) {
    New-NetFirewallRule -DisplayName 'NERI CI SSH' -Direction Inbound -Protocol TCP -LocalPort 22 -Action Allow | Out-Null
}

$random = New-Object byte[] 32
$generator = [Security.Cryptography.RandomNumberGenerator]::Create()
try { $generator.GetBytes($random) } finally { $generator.Dispose() }
$newPassword = 'Neri!' + ([BitConverter]::ToString($random)).Replace('-', '')
$secure = ConvertTo-SecureString $newPassword -AsPlainText -Force
Set-LocalUser -Name Administrator -Password $secure
Remove-ItemProperty -Path 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Winlogon' -Name AutoAdminLogon,DefaultPassword -ErrorAction SilentlyContinue
[IO.File]::WriteAllText("$root\bootstrap.ready", 'ssh-key-only' + [Environment]::NewLine)
