<#
.SYNOPSIS
  Builds Kettle Logic for Windows: the engine and the app's portable logic with CMake, then the WinUI 3 app with
  MSBuild. Run it from a Developer PowerShell for Visual Studio 2022.

.EXAMPLE
  .\apps\windows\build.ps1 -Configuration Release -BoostRoot C:\vcpkg\installed\x64-windows\include
#>
param(
    [ValidateSet('Debug', 'Release')] [string] $Configuration = 'Release',
    [ValidateSet('x64', 'ARM64')] [string] $Platform = 'x64',
    # The folder that holds the boost folder. Not needed when BOOST_ROOT is set.
    [string] $BoostRoot = $env:BOOST_ROOT,
    [switch] $SkipTests
)

$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$root = Resolve-Path (Join-Path $here '..\..')
$build = Join-Path $root 'build\windows-logic'

if ($BoostRoot) { $env:BOOST_ROOT = $BoostRoot }

# 1. The engine and the app's portable logic, and their tests.
cmake -S $here -B $build -G 'Visual Studio 17 2022' -A $Platform
cmake --build $build --config $Configuration
if (-not $SkipTests) {
    ctest --test-dir $build -C $Configuration --output-on-failure
}

# 2. The app. NuGet restores the Windows App SDK and C++/WinRT.
nuget restore (Join-Path $here 'KettleLogic.sln') -PackagesDirectory (Join-Path $here 'packages')
msbuild (Join-Path $here 'KettleLogic.sln') /p:Configuration=$Configuration /p:Platform=$Platform /m

Write-Host "Built: $(Join-Path $root "build\windows-app\$Platform\$Configuration\KettleLogic.exe")"
