param([Parameter(Mandatory = $true)][string]$RequestPath)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$request = Get-Content -LiteralPath $RequestPath -Raw | ConvertFrom-Json
if ($request.schemaVersion -ne 1) { throw 'Unsupported guest request schema.' }
if ($request.jobId -notin @('windows-release-install', 'windows-native-debug')) { throw 'Unsupported Windows job.' }
if ($request.phase -notin @('prepare', 'run')) { throw 'Unsupported Windows job phase.' }
if ($request.sourceSha256 -notmatch '^[0-9a-fA-F]{64}$') { throw 'Invalid source digest.' }

$root = 'C:\CI'
$source = Join-Path (Join-Path $root 'jobs') $request.jobId
$archive = 'C:\Users\Administrator\neri-source.tar'
$toolsVolume = Get-Volume | Where-Object FileSystemLabel -eq 'NERI-TOOLS' | Select-Object -First 1
if (-not $toolsVolume -or -not $toolsVolume.DriveLetter) { throw 'NERI-TOOLS media is unavailable.' }
$tools = "$($toolsVolume.DriveLetter):"

function Assert-Source {
    if (-not (Test-Path -LiteralPath $archive)) { throw 'Source archive was not uploaded.' }
    $digest = (Get-FileHash -Algorithm SHA256 -LiteralPath $archive).Hash
    if ($digest -ine $request.sourceSha256) { throw 'Source archive digest mismatch.' }
}

function Install-Msi([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing offline installer: $path" }
    $process = Start-Process -FilePath msiexec.exe -ArgumentList @('/i', $path, '/qn', '/norestart') -Wait -PassThru
    if ($process.ExitCode -notin @(0, 3010)) { throw "Installer failed: $path ($($process.ExitCode))" }
}

function Prepare-Guest {
    Assert-Source
    if (Test-Path -LiteralPath $source) { Remove-Item -LiteralPath $source -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $source | Out-Null
    & tar.exe -xf $archive -C $source
    if ($LASTEXITCODE -ne 0) { throw 'Source extraction failed.' }
    if (-not (Test-Path -LiteralPath (Join-Path $source 'scripts\ci\jobs.ps1'))) { throw 'Source snapshot has no Windows CI job script.' }
    if (Test-Path -LiteralPath (Join-Path $root 'tools.ready')) { return }

    Install-Msi (Join-Path $tools 'PowerShell7.msi')
    Install-Msi (Join-Path $tools 'Node22.msi')
    $vs = Join-Path $tools 'vs-layout\vs_BuildTools.exe'
    if (-not (Test-Path -LiteralPath $vs)) { throw 'Pinned Visual Studio offline layout is missing.' }
    $arguments = @('--quiet', '--wait', '--norestart', '--noweb', '--add', 'Microsoft.VisualStudio.Workload.VCTools', '--add', 'Microsoft.VisualStudio.Component.VC.CMake.Project', '--includeRecommended')
    $process = Start-Process -FilePath $vs -ArgumentList $arguments -Wait -PassThru
    if ($process.ExitCode -notin @(0, 3010)) { throw "Visual Studio Build Tools installation failed ($($process.ExitCode))." }

    $llvmArchive = Join-Path $tools 'llvm.tar.xz'
    if (-not (Test-Path -LiteralPath $llvmArchive)) { throw 'Pinned LLVM archive is missing.' }
    $llvm = Join-Path $root 'llvm'
    New-Item -ItemType Directory -Force -Path $llvm | Out-Null
    & tar.exe -xf $llvmArchive -C $llvm
    if ($LASTEXITCODE -ne 0) { throw 'LLVM extraction failed.' }
    $llvmConfig = Get-ChildItem -LiteralPath $llvm -Recurse -Filter LLVMConfig.cmake -File | Select-Object -First 1
    if (-not $llvmConfig) { throw 'LLVM archive has no LLVMConfig.cmake.' }
    $prefix = $llvmConfig.Directory.Parent.Parent.Parent.FullName
    [IO.File]::WriteAllText((Join-Path $root 'llvm-prefix.txt'), $prefix, [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $root 'tools.ready'), 'offline-tools-verified' + [Environment]::NewLine)
}

if ($request.phase -eq 'prepare') { Prepare-Guest }
else { Assert-Source }
if (-not (Test-Path -LiteralPath $source)) { throw 'Source snapshot is not prepared.' }
if (-not (Test-Path -LiteralPath (Join-Path $root 'llvm-prefix.txt'))) { throw 'LLVM is not prepared.' }
$env:LLVM_PREFIX = (Get-Content -LiteralPath (Join-Path $root 'llvm-prefix.txt') -Raw).Trim()
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio installation locator is missing.' }
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $vsRoot) { throw 'Visual Studio C++ tools are missing.' }
$cmakeBin = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
$ninjaBin = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja'
if (-not (Test-Path -LiteralPath (Join-Path $cmakeBin 'cmake.exe'))) { throw 'Visual Studio CMake is missing.' }
if (-not (Test-Path -LiteralPath (Join-Path $ninjaBin 'ninja.exe'))) { throw 'Visual Studio Ninja is missing.' }
$env:PATH = "$($env:LLVM_PREFIX)\bin;C:\Program Files\PowerShell\7;C:\Program Files\nodejs;$cmakeBin;$ninjaBin;$($env:PATH)"
$env:RUNNER_TEMP = Join-Path $root 'work'
New-Item -ItemType Directory -Force -Path $env:RUNNER_TEMP | Out-Null
Set-Location -LiteralPath $source
& 'C:\Program Files\PowerShell\7\pwsh.exe' -NoProfile -File 'scripts\ci\jobs.ps1' -Action $request.phase -Job $request.jobId
exit $LASTEXITCODE
