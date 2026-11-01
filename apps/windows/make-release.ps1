<#
.SYNOPSIS
  Builds the Windows installer and, with -Publish, uploads it to GitHub Releases and updates the update list.

.DESCRIPTION
  Runs make-installer.ps1 (build, tests, installer), then writes the Windows entry of updates.json, keeping the entries
  for the other platforms. With -Publish it also creates the app-v<version> release with the installer, and replaces
  updates.json in the "updates" release. Run it from a Developer PowerShell on Windows, with the gh tool signed in
  (gh auth login). See docs\updates.md.

.EXAMPLE
  .\apps\windows\make-release.ps1 -SignCommand 'signtool sign /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 /a $f'
  .\apps\windows\make-release.ps1 -SignCommand '...' -Publish
#>
param(
    [ValidateSet('x64', 'ARM64')] [string] $Platform = 'x64',
    # Signs the installer. Use the certificate the app is signed with. See make-installer.ps1.
    [string] $SignCommand,
    # Signs with a self-signed certificate instead. See make-installer.ps1.
    [switch] $SelfSign,
    # Reuses the last build instead of building the app again.
    [switch] $SkipBuild,
    # Uploads the installer and the update list. Without it, the files are only built.
    [switch] $Publish,
    [string] $Repo = 'Kettle-Logic/Kettle-Logic',
    [string] $Iscc,
    # The folder that holds the boost folder. Not needed when BOOST_ROOT is set.
    [string] $BoostRoot = $env:BOOST_ROOT
)

$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..\..')

# 1. Build the installer.
$installerArgs = @{ Configuration = 'Release'; Platform = $Platform }
if ($SkipBuild) { $installerArgs.SkipBuild = $true }
if ($SignCommand) { $installerArgs.SignCommand = $SignCommand }
if ($SelfSign) { $installerArgs.SelfSign = $true }
if ($Iscc) { $installerArgs.Iscc = $Iscc }
if ($BoostRoot) { $installerArgs.BoostRoot = $BoostRoot }
& (Join-Path $PSScriptRoot 'make-installer.ps1') @installerArgs

$props = [xml](Get-Content (Join-Path $PSScriptRoot 'KettleLogic\KettleLogic.Paths.props') -Raw)
$version = $props.Project.PropertyGroup | ForEach-Object { $_.KettleLogicUiVersion } | Where-Object { $_ } | Select-Object -First 1
$arch = $Platform.ToLowerInvariant()
$installer = Join-Path $root "dist\KettleLogic-Setup-$version-$arch.exe"
if (-not (Test-Path $installer)) { throw "The installer was not built: $installer" }
if ($Publish -and -not $SignCommand -and -not $SelfSign) {
    throw 'Publishing needs a signed installer, because the in-app update installs only an installer signed by the same certificate as the app. Pass -SignCommand or -SelfSign.'
}

# 2. The update list. Start from the published one so the other platforms' entries stay.
$manifest = Join-Path $root 'dist\updates.json'
Remove-Item $manifest -ErrorAction SilentlyContinue
if ($Publish) {
    gh release download updates --repo $Repo --pattern updates.json --output $manifest --clobber 2>$null
    if ($LASTEXITCODE -ne 0) {
        Write-Host 'No update list is published yet, so a new one is started.'
        Remove-Item $manifest -ErrorAction SilentlyContinue
    }
}
$url = "https://github.com/$Repo/releases/download/app-v$version/KettleLogic-Setup-$version-$arch.exe"
python (Join-Path $root 'tools\update_manifest.py') $manifest windows --version $version --url $url --file $installer
if ($LASTEXITCODE -ne 0) { throw 'update_manifest.py failed.' }

Write-Host "Built: $installer"
Write-Host "Update list: $manifest"
if (-not $Publish) {
    Write-Host "Not published. Add -Publish to upload them to $Repo."
    return
}

# 3. Publish. The installer goes on app-v<version>, and the list replaces the one on the fixed "updates" release.
gh release view "app-v$version" --repo $Repo *>$null
if ($LASTEXITCODE -ne 0) {
    gh release create "app-v$version" $installer --repo $Repo --title "Kettle Logic $version" `
        --notes "Kettle Logic $version for Windows."
} else {
    # The release exists, for example because the macOS build published it first. Add this installer to it.
    gh release upload "app-v$version" $installer --repo $Repo --clobber
}
if ($LASTEXITCODE -ne 0) { throw 'Uploading the installer failed.' }

gh release view updates --repo $Repo *>$null
if ($LASTEXITCODE -ne 0) {
    gh release create updates --repo $Repo --prerelease --title 'Update list' `
        --notes 'Where Kettle Logic looks for updates. Do not delete this release.'
}
gh release upload updates $manifest --repo $Repo --clobber
if ($LASTEXITCODE -ne 0) { throw 'Uploading the update list failed.' }
Write-Host "Published. The apps will find the update list at https://github.com/$Repo/releases/download/updates/updates.json"
