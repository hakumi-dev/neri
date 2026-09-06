# Focused Windows process-supervision contracts for native/tools/host_windows.inc.
[CmdletBinding()]
param([Parameter(Mandatory)][string]$HostExecutable)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
$evidence = Join-Path $root "build/windows/host-contract-$PID"
New-Item -ItemType Directory -Force $evidence | Out-Null

function Assert-Equal([object]$Actual, [object]$Expected, [string]$Message) {
  if ($Actual -ne $Expected) { throw "$Message (actual: [$Actual], expected: [$Expected])" }
}
function Run-Host([int]$Seconds, [string]$Name, [string]$Command, [int]$ExpectedStatus) {
  $stdout = Join-Path $evidence "$Name.stdout"
  $stderr = Join-Path $evidence "$Name.stderr"
  $status = Join-Path $evidence "$Name.status"
  $clock = [Diagnostics.Stopwatch]::StartNew()
  & $HostExecutable run $Seconds $stdout $stderr $status powershell.exe -NoProfile -NonInteractive -Command $Command
  Assert-Equal $LASTEXITCODE 0 "neri-host failed for $Name"
  $clock.Stop()
  Assert-Equal (([IO.File]::ReadAllText($status)).Trim()) ([string]$ExpectedStatus) "status mismatch for $Name"
  $metrics = Get-Content -Raw -LiteralPath "$status.metrics.json" | ConvertFrom-Json
  if ($metrics.wallSeconds -lt 0 -or $metrics.userSeconds -lt 0 -or
      $metrics.systemSeconds -lt 0 -or $metrics.peakRssBytes -le 0) {
    throw "invalid process metrics for $Name"
  }
  [pscustomobject]@{Elapsed = $clock.Elapsed; Stdout = [IO.File]::ReadAllText($stdout); Stderr = [IO.File]::ReadAllText($stderr)}
}

$normal = Run-Host 5 'normal' "[Console]::WriteLine('child stdout'); [Console]::Error.WriteLine('child stderr'); exit 23" 23
Assert-Equal $normal.Stdout "child stdout`r`n" 'stdout was not captured separately'
Assert-Equal $normal.Stderr "child stderr`r`n" 'stderr was not captured separately'

$timeout = Run-Host 1 'timeout' 'Start-Sleep -Seconds 8' 124
if ($timeout.Elapsed.TotalSeconds -gt 5) { throw "host timeout exceeded deadline: $($timeout.Elapsed)" }

Write-Output 'Windows host process contracts passed'
