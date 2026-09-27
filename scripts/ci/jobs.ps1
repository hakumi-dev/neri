param(
    [Parameter(Mandatory = $true)][ValidateSet('prepare', 'run')][string]$Action,
    [Parameter(Mandatory = $true)][ValidateSet('windows-release-install', 'windows-native-debug')][string]$Job
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Set-Location $root
if (-not $IsWindows -or [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture -ne 'X64') {
    throw 'Windows CI requires a native x64 Windows guest.'
}
$os = Get-CimInstance Win32_OperatingSystem
if ($os.BuildNumber -ne '26100' -or $os.ProductType -eq 1) {
    throw 'Windows CI requires Windows Server 2025 (build 26100).'
}
if (-not $env:RUNNER_TEMP -or -not [System.IO.Path]::IsPathRooted($env:RUNNER_TEMP)) {
    throw 'Set RUNNER_TEMP to an absolute guest temporary directory.'
}
New-Item -ItemType Directory -Force -Path $env:RUNNER_TEMP | Out-Null
if ($Action -eq 'prepare') {
    foreach ($tool in @('cmake', 'ninja', 'pwsh')) {
        Get-Command $tool -ErrorAction Stop | Out-Null
    }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio Build Tools with vswhere is required.' }
    $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or -not $installation) { throw 'Visual Studio x64 C++ tools are required.' }
    if ($Job -eq 'windows-release-install') {
        # Required by the current Windows protocol driver.
        $version = & node --version
        if ($LASTEXITCODE -ne 0 -or $version -notmatch '^v22\.') { throw 'Node.js 22 is required by the current Windows protocol driver.' }
    }
    return
}
$toolchain = @{}
if ($env:LLVM_PREFIX) {
    $pin = Get-Content -LiteralPath 'bootstrap/windows-x86_64.json' -Raw | ConvertFrom-Json
    $llvmConfig = Join-Path $env:LLVM_PREFIX 'bin/llvm-config.exe'
    if (-not (Test-Path -LiteralPath $llvmConfig)) { throw 'Configured LLVM prefix has no llvm-config.exe.' }
    $llvmVersion = & $llvmConfig --version
    if ($LASTEXITCODE -ne 0 -or $llvmVersion -ne $pin.llvmVersion) { throw 'Configured LLVM version differs from the declared pin.' }
    $toolchain.LLVM = $env:LLVM_PREFIX
}
if ($Job -eq 'windows-release-install') {
    & ./scripts/build.ps1 install -Stage 3 -FullValidation -Prefix "$env:RUNNER_TEMP/neri" -NoPath @toolchain
    if ($LASTEXITCODE -ne 0) { throw 'Windows install validation failed.' }
    & "$env:RUNNER_TEMP/neri/bin/neri.exe" examples/arguments.hk -- Ada 'Grace Hopper'
    if ($LASTEXITCODE -ne 0) { throw 'Installed launcher failed.' }
} else {
    & ./scripts/build.ps1 native -Configuration Debug @toolchain
    if ($LASTEXITCODE -ne 0) { throw 'Windows native validation failed.' }
}
