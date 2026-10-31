<#
.SYNOPSIS
  Builds Kettle Logic for Windows: the engine and the app's portable logic with CMake, then the WinUI 3 app with
  MSBuild. Run it from a Developer PowerShell for Visual Studio 2022 or 2026.

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

# A failing native command does not stop a script by itself, so each one is checked.
function Invoke-Step([string] $Name, [scriptblock] $Command) {
    & $Command
    if ($LASTEXITCODE -ne 0) {
        throw "$Name failed with exit code $LASTEXITCODE."
    }
}

# The newest Visual Studio that this CMake can generate for, with the MSVC toolset that goes with it.
$generators = @(
    @{ Name = 'Visual Studio 18 2026'; Toolset = 'v145' },
    @{ Name = 'Visual Studio 17 2022'; Toolset = 'v143' }
)
$cmakeHelp = (cmake --help) -join "`n"
$vs = $generators | Where-Object { $cmakeHelp -match [regex]::Escape($_.Name) } | Select-Object -First 1
if (-not $vs) {
    throw 'This CMake has no Visual Studio 2022 or 2026 generator. Run this from a Developer PowerShell for Visual Studio.'
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
