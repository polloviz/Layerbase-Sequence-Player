# Writes the winget manifests for the current version into dist\winget\manifests\l\Layerbase\SequencePlayer\<version>.
#   .\tools\make_winget.ps1 -InstallerUrl https://github.com/<owner>/<repo>/releases/download/v1.2.0/LayerbaseSequencePlayer-1.2.0-Setup.exe
# The URL must serve exactly dist\LayerbaseSequencePlayer-<version>-Setup.exe (the hash is computed from it):
# upload the final (signed) installer first, then run this script, then submit the folder to
# https://github.com/microsoft/winget-pkgs (see README "Distribuzione").
param(
    [Parameter(Mandatory = $true)][string]$InstallerUrl,
    [string]$ReleaseNotes = "",
    [string]$Root = (Split-Path -Parent $PSScriptRoot)
)
$ErrorActionPreference = "Stop"

$version = (Select-String -Path "$Root\CMakeLists.txt" -Pattern 'project\(SequencePlayer VERSION ([\d.]+)').Matches[0].Groups[1].Value
$setup = "$Root\dist\LayerbaseSequencePlayer-$version-Setup.exe"
if (-not (Test-Path $setup)) { throw "Missing $setup - build the installer first" }
$sha = (Get-FileHash $setup -Algorithm SHA256).Hash
if (-not $ReleaseNotes) {
    $json = Get-Content "$Root\installer\version.json" -Raw | ConvertFrom-Json
    $ReleaseNotes = $json.notes_en
}

$id = "Layerbase.SequencePlayer"
$mv = "1.6.0"
$dir = "$Root\dist\winget\manifests\l\Layerbase\SequencePlayer\$version"
New-Item -ItemType Directory -Force $dir | Out-Null
$enc = New-Object Text.UTF8Encoding($false)
function Write-Yaml($file, $schema, $body) {
    $text = "# yaml-language-server: `$schema=https://aka.ms/winget-manifest.$schema.$mv.schema.json`n`n" + $body.Trim() + "`n"
    [IO.File]::WriteAllText("$dir\$file", $text.Replace("`r`n", "`n"), $enc)
}

Write-Yaml "$id.yaml" "version" @"
PackageIdentifier: $id
PackageVersion: $version
DefaultLocale: en-US
ManifestType: version
ManifestVersion: $mv
"@

Write-Yaml "$id.installer.yaml" "installer" @"
PackageIdentifier: $id
PackageVersion: $version
Platform:
- Windows.Desktop
MinimumOSVersion: 10.0.17763.0
InstallerType: inno
InstallModes:
- interactive
- silent
- silentWithProgress
UpgradeBehavior: install
ProductCode: '{7C4B8E1A-3F2D-4B7E-9A51-6E2F0C9D8B34}_is1'
FileExtensions:
- bmp
- dpx
- exr
- hdr
- jpeg
- jpg
- png
- psd
- tga
- tif
- tiff
Installers:
- Architecture: x64
  Scope: user
  InstallerUrl: $InstallerUrl
  InstallerSha256: $sha
  InstallerSwitches:
    Custom: /CURRENTUSER
- Architecture: x64
  Scope: machine
  InstallerUrl: $InstallerUrl
  InstallerSha256: $sha
  InstallerSwitches:
    Custom: /ALLUSERS
ManifestType: installer
ManifestVersion: $mv
"@

$notes = ($ReleaseNotes -split "`n" | ForEach-Object { "  " + $_.TrimEnd() }) -join "`n"
Write-Yaml "$id.locale.en-US.yaml" "defaultLocale" @"
PackageIdentifier: $id
PackageVersion: $version
PackageLocale: en-US
Publisher: Layerbase Luxury Vision
PublisherUrl: https://layerbase.it
PublisherSupportUrl: https://github.com/polloviz/Layerbase-Sequence-Player/issues
Author: Layerbase Luxury Vision
PackageName: Layerbase Sequence Player
PackageUrl: https://github.com/polloviz/Layerbase-Sequence-Player
License: MIT
LicenseUrl: https://github.com/polloviz/Layerbase-Sequence-Player/blob/main/LICENSE.txt
Copyright: (c) 2026 Layerbase Luxury Vision
ReleaseNotesUrl: https://github.com/polloviz/Layerbase-Sequence-Player/releases
ShortDescription: Fast image sequence player for EXR, DPX and more, with OCIO, ACES 2.0, AgX, Cryptomatte and movie export.
Description: |-
  Layerbase Sequence Player opens image sequences (EXR, DPX, TIFF, PNG, JPEG...) instantly and plays them
  with GPU color management through OpenColorIO: ACES 2.0 and 1.3, built-in AgX, Blender and custom configs.
  It shows multi-layer EXR passes, isolates objects with Cryptomatte (including separate Cryptomatte
  sequences such as Octane's), and exports H.264, H.265 and ProRes (4444 with alpha) movies, one by one
  or in batch. Free and open source (MIT), also for commercial use.
Moniker: sequenceplayer
Tags:
- aces
- cryptomatte
- dpx
- exr
- image-sequence
- ocio
- openexr
- player
- prores
- vfx
ReleaseNotes: |-
$notes
ManifestType: defaultLocale
ManifestVersion: $mv
"@

Write-Host "winget manifests: $dir"
Write-Host "Validate with: winget validate `"$dir`"   and test with: winget install --manifest `"$dir`""
