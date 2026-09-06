# The IDE stops the launcher process; its compiler child must not be orphaned.
[CmdletBinding()]
param([Parameter(Mandatory)][string]$Compiler)
$ErrorActionPreference = 'Stop'
$process = [Diagnostics.Process]::new()
$process.StartInfo.FileName = [IO.Path]::GetFullPath($Compiler)
$process.StartInfo.ArgumentList.Add('lsp')
$process.StartInfo.UseShellExecute = $false
$process.StartInfo.CreateNoWindow = $true
$process.StartInfo.RedirectStandardInput = $true
$process.StartInfo.RedirectStandardOutput = $true
$process.StartInfo.RedirectStandardError = $true
$child = $null
$started = $false
try {
  if (!$process.Start()) { throw 'Cannot start launcher' }
  $started = $true
  $deadline = [DateTime]::UtcNow.AddSeconds(10)
  do {
    $children = @(Get-CimInstance Win32_Process -Filter "ParentProcessId = $($process.Id)" |
      Where-Object Name -eq 'neri-compiler.exe')
    if ($children.Count -gt 0) { $child = [Diagnostics.Process]::GetProcessById($children[0].ProcessId); break }
    if ($process.HasExited) { throw "Launcher exited early: $($process.StandardError.ReadToEnd())" }
    Start-Sleep -Milliseconds 100
  } while ([DateTime]::UtcNow -lt $deadline)
  if (!$child) { throw 'Launcher did not start the language server' }
  $process.Kill()
  if (!$process.WaitForExit(5000)) { throw 'Launcher did not terminate' }
  if (!$child.WaitForExit(5000)) { throw 'Stopping launcher left an orphaned language server' }
  Write-Output 'Windows launcher process-tree cleanup passed'
} finally {
  if ($child) {
    if (!$child.HasExited) { $child.Kill() }
    $child.Dispose()
  }
  if ($started -and !$process.HasExited) { $process.Kill() }
  $process.Dispose()
}
