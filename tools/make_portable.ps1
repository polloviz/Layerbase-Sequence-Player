# Builds dist\LayerbaseSequencePlayer-<version>-Portable.zip from build\Release and third_party.
# The zip holds one folder; portable.txt next to the exe keeps settings in its "data" subfolder.
param([string]$Root = (Split-Path -Parent $PSScriptRoot))
$ErrorActionPreference = "Stop"

$version = (Select-String -Path "$Root\CMakeLists.txt" -Pattern 'project\(SequencePlayer VERSION ([\d.]+)').Matches[0].Groups[1].Value
$name = "Layerbase Sequence Player"
$stageRoot = Join-Path $env:TEMP "LayerbaseSequencePlayer-portable"
$stage = Join-Path $stageRoot $name
if (Test-Path $stageRoot) { Remove-Item $stageRoot -Recurse -Force }
New-Item -ItemType Directory -Force $stage | Out-Null

Copy-Item "$Root\build\Release\SequencePlayer.exe" $stage
Copy-Item "$Root\third_party\ffmpeg\ffmpeg.exe", "$Root\third_party\ffmpeg\FFMPEG_LICENSE.txt", "$Root\third_party\ffmpeg\FFMPEG_README.txt" $stage
Copy-Item "$Root\LICENSE.txt", "$Root\LICENSE_IT.txt", "$Root\FEATURES.md", "$Root\res\THIRD_PARTY_NOTICES.txt" $stage

@"
Layerbase Sequence Player $version - portable
=============================================

Run SequencePlayer.exe. Nothing is installed and nothing is written to the
registry: settings are saved in the "data" folder next to the program.
Delete this file (portable.txt) to use %APPDATA%\SequencePlayer instead.

To make the portable copy appear in "Open with", use Settings > Register file
types (Settings > Remove registration undoes it).

Versione portable: avvia SequencePlayer.exe. Non viene installato nulla e il
registro non viene modificato: le impostazioni sono salvate nella cartella
"data" accanto al programma. Elimina questo file per usare %APPDATA%.

Open source (MIT) by Layerbase Luxury Vision - https://layerbase.it
https://github.com/polloviz/Layerbase-Sequence-Player
"@ | Set-Content "$stage\portable.txt" -Encoding utf8

New-Item -ItemType Directory -Force "$Root\dist" | Out-Null
$zip = "$Root\dist\LayerbaseSequencePlayer-$version-Portable.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path $stage -DestinationPath $zip -CompressionLevel Optimal
Remove-Item $stageRoot -Recurse -Force
Write-Host "Portable: $zip"
