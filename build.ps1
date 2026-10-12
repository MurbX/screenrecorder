# Builds ScreenRec with the Visual Studio C++ toolchain (Ninja + MSVC).
#   .\build.ps1            -> Release build in .\build
#   .\build.ps1 -Config Debug
param([string]$Config = "Release")
$ErrorActionPreference = "Stop"

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw "Visual Studio with the C++ workload was not found." }

Import-Module "$vs\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments "-arch=x64 -host_arch=x64" | Out-Null

$build = Join-Path $PSScriptRoot "build"
cmake -S $PSScriptRoot -B $build -G Ninja "-DCMAKE_BUILD_TYPE=$Config"
if ($LASTEXITCODE) { exit $LASTEXITCODE }
cmake --build $build
exit $LASTEXITCODE
