<#
.SYNOPSIS
  Builds the Windows installer, KettleLogic-Setup-<version>-<arch>.exe in dist\, with Inno Setup 7.

.DESCRIPTION
  Builds the app with build.ps1, then compiles installer\KettleLogic.iss. The version is read from
  KettleLogic.Paths.props, so the installer and the app agree. Run it from a Developer PowerShell, on Windows, after
  installing Inno Setup 7 (https://jrsoftware.org/isinfo.php).

  With -SignCommand, the installer and its uninstaller are signed. Give the command with $f where the file goes, for
  example:
    -SignCommand 'signtool sign /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 /a $f'
  The certificate must be the one the app is signed with, because the in-app update checks that they match. The program
  (KettleLogic.exe), the installer, and the uninstaller are all signed with it.

  With -SelfSign, there is no certificate to buy. The script makes a self-signed code-signing certificate in your user
  certificate store (or reuses the one it made before), signs with it, and writes its public part to
  dist\KettleLogic-SelfSigned.cer. Windows does not trust a self-signed certificate, so SmartScreen still warns, and the
  in-app update works only on a computer that has trusted the .cer file. See docs\updates.md.

.EXAMPLE
  .\apps\windows\make-installer.ps1 -Configuration Release -Platform x64 -SignCommand '...'
  .\apps\windows\make-installer.ps1 -Configuration Release -Platform x64 -SelfSign
#>
param(
    [ValidateSet('Debug', 'Release')] [string] $Configuration = 'Release',
    [ValidateSet('x64', 'ARM64')] [string] $Platform = 'x64',
    # Reuses the last build instead of building the app again.
    [switch] $SkipBuild,
    # Signs the installer. See the description above.
    [string] $SignCommand,
    # Signs with a self-signed certificate made on this computer. Not for use with -SignCommand.
    [switch] $SelfSign,
    # Where ISCC.exe is, if it is not in Program Files.
    [string] $Iscc,
    # The folder that holds the boost folder. Not needed when BOOST_ROOT is set.
    [string] $BoostRoot = $env:BOOST_ROOT
)

$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$root = Resolve-Path (Join-Path $here '..\..')

# 1. The app. build.ps1 builds the engine, the logic, its tests, and the WinUI app.
if (-not $SkipBuild) {
    & (Join-Path $here 'build.ps1') -Configuration $Configuration -Platform $Platform -BoostRoot $BoostRoot
}

$source = Join-Path $root "build\windows-app\$Platform\$Configuration"
if (-not (Test-Path (Join-Path $source 'KettleLogic.exe'))) {
    throw "The app is not built in $source. Run without -SkipBuild first."
}

# 2. The version, from the same file the app reads.
$props = [xml](Get-Content (Join-Path $here 'KettleLogic\KettleLogic.Paths.props') -Raw)
$version = $props.Project.PropertyGroup | ForEach-Object { $_.KettleLogicUiVersion } | Where-Object { $_ } | Select-Object -First 1
if (-not $version) { throw 'KettleLogicUiVersion is not set in KettleLogic.Paths.props.' }

# 3. Inno Setup.
if (-not $Iscc) {
    $candidates = @(
        (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 7\ISCC.exe'),
        (Join-Path $env:ProgramFiles 'Inno Setup 7\ISCC.exe')
    )
    $Iscc = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $Iscc) {
        $Iscc = (Get-Command ISCC.exe -ErrorAction SilentlyContinue).Source
    }
}
if (-not $Iscc) { throw 'Inno Setup 7 was not found. Install it, or pass -Iscc with the path to ISCC.exe.' }

$arch = $Platform.ToLowerInvariant()
$dist = Join-Path $root 'dist'
New-Item -ItemType Directory -Force -Path $dist | Out-Null

if ($SelfSign) {
    if ($SignCommand) { throw 'Pass either -SelfSign or -SignCommand, not both.' }
    # Reuse the certificate from an earlier build, because the in-app update needs every release signed by the same one.
    $subject = 'CN=Kettle Logic (self-signed)'
    $cert = Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert |
        Where-Object { $_.Subject -eq $subject -and $_.NotAfter -gt (Get-Date).AddDays(30) } |
        Sort-Object NotAfter -Descending | Select-Object -First 1
    if (-not $cert) {
        Write-Host "Making a self-signed code-signing certificate: $subject"
        $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject $subject -CertStoreLocation Cert:\CurrentUser\My `
            -KeyAlgorithm RSA -KeyLength 3072 -HashAlgorithm SHA256 -NotAfter (Get-Date).AddYears(5)
    }
    $cerFile = Join-Path $dist 'KettleLogic-SelfSigned.cer'
    Export-Certificate -Cert $cert -FilePath $cerFile -Force | Out-Null
    $SignCommand = "signtool sign /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 /sha1 $($cert.Thumbprint) `$f"
    Write-Host "Signing with the self-signed certificate $($cert.Thumbprint). Its public part is $cerFile."
}

$arguments = @(
    "/DAppVersion=$version",
    "/DArch=$arch",
    "/DSourceDir=$source",
    "/DOutputDir=$dist"
)
if ($SignCommand) {
    $arguments += '/DSignCommand=1'
    $arguments += "/Ssigner=$SignCommand"
}
$arguments += (Join-Path $here 'installer\KettleLogic.iss')

& $Iscc @arguments
if ($LASTEXITCODE -ne 0) { throw "ISCC failed with exit code $LASTEXITCODE." }

$installer = Join-Path $dist "KettleLogic-Setup-$version-$arch.exe"
Write-Host "Built: $installer"
Write-Host ''
if ($SelfSign) {
    Write-Host 'The certificate is self-signed, so Windows trusts it only where you add it. On each computer that should accept'
    Write-Host 'the installer and in-app updates, run this in an administrator PowerShell:'
    Write-Host "  Import-Certificate -FilePath '$cerFile' -CertStoreLocation Cert:\LocalMachine\Root"
    Write-Host ''
}
Write-Host 'To publish it, run make-release.ps1 -Publish. See docs\updates.md.'
