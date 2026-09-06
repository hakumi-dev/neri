[CmdletBinding()]
param([string]$LLVM = "$PSScriptRoot/../build/tools/clang+llvm-22.1.8-x86_64-pc-windows-msvc")
$ErrorActionPreference = 'Stop'
$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
if (!(Test-Path $vswhere)) { throw 'Visual Studio C++ tools are required.' }
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$installation) { throw 'Install the Visual Studio Desktop development with C++ workload.' }
Import-Module "$installation/Common7/Tools/Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $installation -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
$env:LLVM_PREFIX = (Resolve-Path $LLVM).Path
$env:PATH = "$env:LLVM_PREFIX/bin;$installation/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin;$installation/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja;$env:PATH"
