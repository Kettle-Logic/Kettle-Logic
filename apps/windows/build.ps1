<#
.SYNOPSIS
  Builds Kettle Logic for Windows: the engine and the app's portable logic with CMake, then the WinUI 3 app with
  MSBuild. Run it from a Developer PowerShell for Visual Studio 2026.

.EXAMPLE
  .\apps\windows\build.ps1 -Configuration Release -BoostRoot C:\boost
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

# Without a Boost folder, CMake searches for one and can find MSYS2's MinGW Boost, whose headers break the MSVC build with
# hundreds of syntax errors. Stop here with the fix instead.
if (-not $env:BOOST_ROOT -or -not (Test-Path (Join-Path $env:BOOST_ROOT 'boost\version.hpp'))) {
    throw 'Boost was not found. Pass -BoostRoot (or set BOOST_ROOT) to the folder that holds the boost folder, for example C:\vcpkg\installed\x64-windows\include.'
}

# A failing native command does not stop a script by itself, so each one is checked.
function Invoke-Step([string] $Name, [scriptblock] $Command) {
    & $Command
    if ($LASTEXITCODE -ne 0) {
        throw "$Name failed with exit code $LASTEXITCODE."
    }
}

# Visual Studio 2026, with the MSVC toolset that goes with it.
$vs = @{ Name = 'Visual Studio 18 2026'; Toolset = 'v145' }
$cmakeHelp = (cmake --help) -join "`n"
if ($cmakeHelp -notmatch [regex]::Escape($vs.Name)) {
    throw 'This CMake has no Visual Studio 2026 generator. Run this from a Developer PowerShell for Visual Studio 2026.'
}
Write-Host "Using $($vs.Name) ($($vs.Toolset))."

# 1. The engine and the app's portable logic, and their tests.
Invoke-Step 'CMake configure' { cmake -S $here -B $build -G $vs.Name -A $Platform }
Invoke-Step 'CMake build' { cmake --build $build --config $Configuration }
if (-not $SkipTests) {
    Invoke-Step 'Tests' { ctest --test-dir $build -C $Configuration --output-on-failure }
}

# 2. The app. NuGet restores the Windows App SDK, WebView2, and C++/WinRT.
if (-not (Get-Command nuget -ErrorAction SilentlyContinue)) {
    throw 'nuget.exe is not on the path. Install it from https://www.nuget.org/downloads and run this again.'
}
$sln = Join-Path $here 'KettleLogic.sln'
Invoke-Step 'NuGet restore' { nuget restore $sln -PackagesDirectory (Join-Path $here 'packages') }
Invoke-Step 'MSBuild' {
    msbuild $sln /p:Configuration=$Configuration /p:Platform=$Platform /p:PlatformToolset=$($vs.Toolset) /m
}

Write-Host "Built: $(Join-Path $root "build\windows-app\$Platform\$Configuration\KettleLogic.exe")"
