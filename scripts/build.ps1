#requires -Version 7.0
[CmdletBinding()]
param(
  [ValidateSet('doctor','native','build','test','install')][string]$Action = 'install',
  [ValidateSet('Release','Debug')][string]$Configuration = 'Release',
  [string]$Prefix = "$env:USERPROFILE/.neri",
  [string]$LLVM,
  [switch]$NoPath
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
function Invoke-Checked([string]$Program, [string[]]$Arguments) {
  & $Program @Arguments
  if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}
function Assert-Hash([string]$File, [string]$Expected) {
  if ((Get-FileHash -LiteralPath $File -Algorithm SHA256).Hash -ine $Expected) { throw "SHA-256 mismatch: $File" }
}
function Forward-Path([string]$Value) { return $Value.Replace('\','/') }

$root = Split-Path $PSScriptRoot
Push-Location $root
try {
  if ($env:OS -ne 'Windows_NT' -or [Runtime.InteropServices.RuntimeInformation]::OSArchitecture -ne 'X64') {
    throw 'This entry point requires native Windows x64.'
  }
  $pin = Get-Content bootstrap/windows-x86_64.json -Raw | ConvertFrom-Json
  $downloads = Join-Path $root 'build/downloads'
  $tools = Join-Path $root 'build/tools'
  New-Item -ItemType Directory -Force $downloads,$tools | Out-Null
  if (!$LLVM) {
    $LLVM = Join-Path $tools ($pin.llvmArchive -replace '\.tar\.xz$','')
    if (!(Test-Path "$LLVM/lib/cmake/llvm/LLVMConfig.cmake")) {
      $archive = Join-Path $downloads $pin.llvmArchive
      if (!(Test-Path $archive)) {
        Write-Host "Downloading LLVM $($pin.llvmVersion) development archive..."
        Invoke-WebRequest -Uri "https://github.com/llvm/llvm-project/releases/download/llvmorg-$($pin.llvmVersion)/$($pin.llvmArchive)" -OutFile $archive
      }
      Assert-Hash $archive $pin.llvmSha256
      Invoke-Checked tar @('-xf',$archive,'-C',$tools)
    }
  }
  . "$PSScriptRoot/windows-env.ps1" -LLVM $LLVM
  Invoke-Checked clang @('--version')
  Invoke-Checked cmake @('--version')
  Invoke-Checked ninja @('--version')
  if ($Action -eq 'doctor') { return }
  $mode = $Configuration.ToLowerInvariant()
  $native = Join-Path $root "build/native/windows-$mode"
  Invoke-Checked cmake @('-S',$root,'-B',$native,'-G','Ninja',"-DCMAKE_BUILD_TYPE=$Configuration",'-DCMAKE_C_COMPILER=clang','-DCMAKE_CXX_COMPILER=clang++',"-DLLVM_DIR=$env:LLVM_PREFIX/lib/cmake/llvm",'-DBUILD_TESTING=ON')
  Invoke-Checked cmake @('--build',$native,'--parallel','4')
  Invoke-Checked ctest @('--test-dir',$native,'--output-on-failure','--no-tests=error')
  if ($Action -eq 'native') { return }

  $seed = Get-Content "$root/bootstrap/seed.json" -Raw | ConvertFrom-Json
  if ($seed.schemaVersion -ne 1 -or $seed.format -ne 'neri-ir-binary-gzip' -or
      $seed.artifact -ne 'compiler.nir.gz' -or $seed.sourceManifest -ne 'SOURCE-MANIFEST.sha256') {
    throw 'Unsupported canonical bootstrap seed metadata'
  }
  Assert-Hash "$root/bootstrap/compiler.nir.gz" $seed.artifactSha256
  Assert-Hash "$root/bootstrap/SOURCE-MANIFEST.sha256" $seed.sourceManifestSha256
  Assert-Hash "$root/bootstrap/VALIDATION-SOURCE-MANIFEST.sha256" $seed.validationSourceManifestSha256
  $work = Join-Path $root ("build/windows/work-" + [Guid]::NewGuid().ToString('N'))
  New-Item -ItemType Directory -Force $work | Out-Null
  $compressed = [IO.File]::OpenRead("$root/bootstrap/compiler.nir.gz")
  try {
    $gzip = [IO.Compression.GZipStream]::new($compressed, [IO.Compression.CompressionMode]::Decompress)
    try {
      $binary = [IO.File]::Create("$work/seed.nir")
      try { $gzip.CopyTo($binary) } finally { $binary.Dispose() }
    } finally { $gzip.Dispose() }
  } finally { $compressed.Dispose() }
  Assert-Hash "$work/seed.nir" $seed.irSha256
  $env:NERI_STDLIB = Forward-Path "$root/stdlib"
  $env:NERI_HOST = Forward-Path "$native/neri-host.exe"
  $env:NERI_CODEGEN = Forward-Path "$native/neri-codegen.exe"
  $env:NERI_RUNTIME_MANIFEST = Forward-Path "$native/neri-runtime-windows-x86_64.json"
  $env:NERI_LINKER = Forward-Path "$env:LLVM_PREFIX/bin/clang++.exe"
  $env:TMPDIR = Forward-Path $work
  function Compiler-Sources {
    @(Get-ChildItem -LiteralPath "$root/compiler" -Recurse -Filter '*.hk' -File |
      Where-Object { (Forward-Path $_.FullName) -notmatch '/(frontend|semantic)/main\.hk$|/compiler/session/' } |
      ForEach-Object { Forward-Path $_.FullName } | Sort-Object)
  }
  $sources = @(Compiler-Sources)
  [IO.File]::WriteAllLines("$work/compiler-sources.txt", $sources)
  $templateAssets = @("$root/share/neri/templates/declarations.json", "$root/share/neri/templates/declarations.schema.json")
  function Compiler-Inventory {
    $library = @(Get-ChildItem -LiteralPath "$root/stdlib" -Recurse -File |
      Where-Object { $_.Extension -in '.hk','.json' } | ForEach-Object { Forward-Path $_.FullName })
    $inputs = @((Compiler-Sources) + $library + $templateAssets + @("$root/manifest.json", "$root/.editorconfig") | Sort-Object -Unique)
    @($inputs | ForEach-Object { (Get-FileHash -LiteralPath $_).Hash + '  ' + $_ })
  }
  $inventory = @(Compiler-Inventory)
  [IO.File]::WriteAllLines("$work/SOURCE-MANIFEST.sha256", $inventory)

  function Materialize([string]$IR, [string]$Format, [string]$Stage) {
    New-Item -ItemType Directory -Force $Stage | Out-Null
    Invoke-Checked $env:NERI_CODEGEN @('--input',$IR,'--input-format',$Format,'--target','windows-x86_64','--optimization','release','--emit','object','--output',"$Stage/compiler.obj")
    Invoke-Checked $env:NERI_LINKER @("$Stage/compiler.obj","$native/neri-runtime.lib",'-o',"$Stage/neri.exe",'-lws2_32','-lbcrypt','-lshell32','-Wl,/Brepro')
  }
  Materialize "$work/seed.nir" 'binary' "$work/stage0"
  for ($generation = 1; $generation -le 3; $generation++) {
    $stage = "$work/stage$generation"
    New-Item -ItemType Directory -Force $stage | Out-Null
    $previous = "$work/stage$($generation - 1)/neri.exe"
    Write-Host "Compiling generation $generation on Windows..."
    $buildArguments = @('build','--project',"$root/manifest.json",'--unit','compiler','--source-root',"$root/compiler")
    Invoke-Checked $previous ($buildArguments + @('--module','neri-compiler','--emit=neri-ir-hex','--output',"$stage/compiler.nir.hex"))
    Materialize "$stage/compiler.nir.hex" 'hex' $stage
  }
  foreach ($artifact in @('compiler.nir.hex','compiler.obj','neri.exe')) {
    Assert-Hash "$work/stage3/$artifact" (Get-FileHash "$work/stage2/$artifact").Hash
  }
  $after = @(Compiler-Inventory)
  if (Compare-Object $inventory $after) { throw 'Compiler sources changed during bootstrap' }
  Write-Host 'Verified native Windows compiler fixed point (IR, COFF and PE).'

  $tree = "$work/toolchain"
  New-Item -ItemType Directory -Force "$tree/bin","$tree/libexec","$tree/lib" | Out-Null
  Copy-Item "$native/neri.exe" "$tree/bin/neri.exe"
  Copy-Item "$work/stage3/neri.exe" "$tree/libexec/neri-compiler.exe"
  Copy-Item "$native/neri-codegen.exe","$native/neri-host.exe" "$tree/libexec"
  Copy-Item "$native/neri-runtime.lib","$native/neri-runtime-windows-x86_64.json" "$tree/lib"
  New-Item -ItemType Directory -Force "$tree/include/neri" | Out-Null
  Copy-Item "$root/native/include/neri/runtime_abi.h","$root/native/include/neri/abi_catalog.h" "$tree/include/neri"
  Copy-Item "$root/stdlib" "$tree/stdlib" -Recurse
  New-Item -ItemType Directory -Force "$tree/share/neri/templates" | Out-Null
  Copy-Item $templateAssets "$tree/share/neri/templates"
  $runtimeLLVM = $env:LLVM_PREFIX
  $prefixPath = [IO.Path]::GetFullPath($Prefix)
  if ($Action -eq 'install') {
    # Installed programs must survive cleaning the checkout's build directory.
    # Linking Neri objects needs the Clang driver and its resource libraries,
    # not the complete 4 GB LLVM development SDK used to build neri-codegen.
    $clangHash = (Get-FileHash "$env:LLVM_PREFIX/bin/clang++.exe").Hash.ToLowerInvariant()
    $runtimeLLVM = Join-Path $prefixPath "dependencies/llvm-$($pin.llvmVersion)-$clangHash"
    New-Item -ItemType Directory -Force "$runtimeLLVM/bin","$runtimeLLVM/lib" | Out-Null
    if (!(Test-Path "$runtimeLLVM/bin/clang++.exe")) { Copy-Item "$env:LLVM_PREFIX/bin/clang++.exe" "$runtimeLLVM/bin/clang++.exe" }
    Assert-Hash "$runtimeLLVM/bin/clang++.exe" $clangHash
    if (!(Test-Path "$runtimeLLVM/bin/lld-link.exe")) { Copy-Item "$env:LLVM_PREFIX/bin/lld-link.exe" "$runtimeLLVM/bin/lld-link.exe" }
    Assert-Hash "$runtimeLLVM/bin/lld-link.exe" (Get-FileHash "$env:LLVM_PREFIX/bin/lld-link.exe").Hash
    if (!(Test-Path "$runtimeLLVM/lib/clang")) { Copy-Item "$env:LLVM_PREFIX/lib/clang" "$runtimeLLVM/lib/clang" -Recurse }
    foreach ($resource in Get-ChildItem "$env:LLVM_PREFIX/lib/clang" -Recurse -File) {
      $relative = $resource.FullName.Substring($env:LLVM_PREFIX.Length + 1)
      Assert-Hash (Join-Path $runtimeLLVM $relative) (Get-FileHash -LiteralPath $resource.FullName).Hash
    }
  }
  [IO.File]::WriteAllLines("$tree/libexec/toolchain.env", @("LLVM_PREFIX=$runtimeLLVM","LIB=$env:LIB","INCLUDE=$env:INCLUDE"))
  Invoke-Checked "$tree/bin/neri.exe" @('--version')
  Invoke-Checked "$tree/bin/neri.exe" @("$root/examples/hello.hk")
  if ($Action -eq 'build') { Write-Host "Toolchain: $tree/bin/neri.exe"; return }
  Invoke-Checked node @("$PSScriptRoot/test-windows.mjs","$tree/bin/neri.exe",$native)
  Invoke-Checked "$tree/bin/neri.exe" @('run','--project',"$root/tests/native/cabi-exports/manifest.json",'--unit','driver','--',"$tree/bin/neri.exe",$root,$native,"$env:LLVM_PREFIX/bin",'windows-x86_64',"$work/cabi-exports")
  & "$root/tests/windows-launcher.ps1" -Compiler "$tree/bin/neri.exe"
  [IO.File]::WriteAllText("$work/VALIDATED", "Windows native, language, UTF-8 paths and LSP contracts passed.`n")
  if ($Action -eq 'test') { Write-Host "Validated toolchain: $tree/bin/neri.exe"; return }

  # Keep previous installations intact; publish a small pointer only after validation.
  $manifest = @(Get-ChildItem $tree -File -Recurse | Sort-Object FullName | ForEach-Object {
    (Get-FileHash -LiteralPath $_.FullName).Hash.ToLowerInvariant() + '  ' + (Forward-Path $_.FullName.Substring($tree.Length + 1))
  })
  [IO.File]::WriteAllLines("$tree/ARTIFACTS.sha256", $manifest)
  $identity = (Get-FileHash "$tree/ARTIFACTS.sha256").Hash.ToLowerInvariant()
  $name = "windows-x86_64-$identity"
  $destination = Join-Path $prefixPath "toolchains/$name"
  New-Item -ItemType Directory -Force "$prefixPath/bin","$prefixPath/toolchains" | Out-Null
  if (!(Test-Path $destination)) { Copy-Item $tree $destination -Recurse }
  foreach ($line in $manifest) {
    $parts = $line -split '  ',2
    Assert-Hash (Join-Path $destination $parts[1]) $parts[0]
  }
  Invoke-Checked "$destination/bin/neri.exe" @("$root/examples/hello.hk")
  $oldSelection = if (Test-Path "$prefixPath/toolchain.txt") { [IO.File]::ReadAllText("$prefixPath/toolchain.txt") } else { $null }
  $hadLauncher = Test-Path "$prefixPath/bin/neri.exe"
  if ($hadLauncher) { Copy-Item "$prefixPath/bin/neri.exe" "$work/previous-launcher.exe" }
  try {
    Copy-Item "$tree/bin/neri.exe" "$prefixPath/bin/neri.next.exe" -Force
    [IO.File]::Move("$prefixPath/bin/neri.next.exe", "$prefixPath/bin/neri.exe", $true)
    [IO.File]::WriteAllText("$prefixPath/toolchain.next", $name)
    [IO.File]::Move("$prefixPath/toolchain.next", "$prefixPath/toolchain.txt", $true)
    Invoke-Checked "$prefixPath/bin/neri.exe" @("$root/examples/hello.hk")
  } catch {
    if ($null -ne $oldSelection) { [IO.File]::WriteAllText("$prefixPath/toolchain.txt", $oldSelection) }
    elseif (Test-Path "$prefixPath/toolchain.txt") { Remove-Item -LiteralPath "$prefixPath/toolchain.txt" }
    if ($hadLauncher) { Copy-Item "$work/previous-launcher.exe" "$prefixPath/bin/neri.exe" -Force }
    elseif (Test-Path "$prefixPath/bin/neri.exe") { Remove-Item -LiteralPath "$prefixPath/bin/neri.exe" }
    throw
  }
  if (!$NoPath) {
    $bin = "$prefixPath\bin"
    $userPath = [Environment]::GetEnvironmentVariable('Path','User')
    if (!(($userPath -split ';') | Where-Object { $_.TrimEnd('\') -ieq $bin })) {
      [Environment]::SetEnvironmentVariable('Path', "$bin;$userPath", 'User')
    }
    $env:PATH = "$bin;$env:PATH"
    Add-Type -Namespace NeriSetup -Name EnvironmentBroadcast -MemberDefinition '[System.Runtime.InteropServices.DllImport("user32.dll", CharSet=System.Runtime.InteropServices.CharSet.Unicode)] public static extern System.IntPtr SendMessageTimeout(System.IntPtr window, uint message, System.UIntPtr parameter, string text, uint flags, uint timeout, out System.UIntPtr result);'
    $result = [UIntPtr]::Zero
    [void][NeriSetup.EnvironmentBroadcast]::SendMessageTimeout([IntPtr]65535,26,[UIntPtr]::Zero,'Environment',2,2000,[ref]$result)
  }
  Write-Host "Installed: $prefixPath/bin/neri.exe"
  Write-Host 'Open a new terminal (and restart Rider) to pick up PATH changes.'
} finally { Pop-Location }
