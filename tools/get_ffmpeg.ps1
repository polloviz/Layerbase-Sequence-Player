# Downloads the bundled FFmpeg build into third_party\ffmpeg (ffmpeg.exe is too large for git).
# Pinned to the build described in third_party\ffmpeg\FFMPEG_README.txt; the SHA-256 is verified.
param([string]$Root = (Split-Path -Parent $PSScriptRoot))
$ErrorActionPreference = "Stop"

$url = "https://github.com/BtbN/FFmpeg-Builds/releases/download/autobuild-2026-09-25-15-37/ffmpeg-n8.1.3-win64-gpl-8.1.zip"
$sha = "96D0DD139BA60446F8DDE48B42ED1D0BA65D30AED3A1B7D953FAFE6E0EAE58A7"
$dst = "$Root\third_party\ffmpeg\ffmpeg.exe"

if ((Test-Path $dst) -and (Get-FileHash $dst).Hash -eq $sha) { Write-Host "FFmpeg already present"; return }
$tmp = Join-Path $env:TEMP "sp-ffmpeg-download"
if (Test-Path $tmp) { Remove-Item $tmp -Recurse -Force }
New-Item -ItemType Directory $tmp | Out-Null
Write-Host "Downloading $url ..."
$ProgressPreference = "SilentlyContinue"
Invoke-WebRequest $url -OutFile "$tmp\ffmpeg.zip"
Expand-Archive "$tmp\ffmpeg.zip" $tmp
$exe = Get-ChildItem $tmp -Recurse -Filter ffmpeg.exe | Select-Object -First 1
if ((Get-FileHash $exe.FullName).Hash -ne $sha) { throw "FFmpeg hash mismatch: expected $sha" }
Copy-Item $exe.FullName $dst -Force
Remove-Item $tmp -Recurse -Force
Write-Host "FFmpeg: $dst"
