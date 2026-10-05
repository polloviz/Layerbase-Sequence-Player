# Downloads the NVIDIA OptiX 8.0 SDK headers into third_party\optix\include (build only).
# The headers are NVIDIA-licensed and may not be republished, so they are not in git; the
# program uses them to call the OptiX denoiser that ships with the NVIDIA driver.
# Without them the build still succeeds, with the OptiX denoiser reported as unavailable.
# Source: https://github.com/NVIDIA/optix-dev (tag v8.0.0, needs driver R535 or later); SHA-256 verified.
param([string]$Root = (Split-Path -Parent $PSScriptRoot))
$ErrorActionPreference = "Stop"

$tag = "v8.0.0"
$files = [ordered]@{
    "optix.h"                           = "071E3AAE8352D7A53B5D15BF475301A5AC416089CD3B51ADB01FD1F5DEB7D936"
    "optix_host.h"                      = "61E70108183FA230BBEBFA8F8FF7796EC9427B0C16277FEC7B72A30891DEE2CA"
    "optix_types.h"                     = "17B348E2F9F10A7590E97521FF6BC045CABA0F5B45C4E4A642041516DAA15EF6"
    "optix_function_table.h"            = "6FA452956B34BC30B0619EDCF873D2B35D40F063624BF122C708729B6AFDAA74"
    "optix_function_table_definition.h" = "C3CFE0F21BB78B5DDD428559480DB3BE3C33B93CE0769293D261FA4922AE73D6"
    "optix_stubs.h"                     = "A2D8CCBA8EEDFF41DB1EE871A29BA7BFF4B1C3B06F22F0690C467282DEA50AE8"
}
$dst = "$Root\third_party\optix\include"
New-Item -ItemType Directory -Force $dst | Out-Null
$ProgressPreference = "SilentlyContinue"
foreach ($name in $files.Keys) {
    $path = "$dst\$name"
    if ((Test-Path $path) -and (Get-FileHash $path).Hash -eq $files[$name]) { continue }
    Write-Host "Downloading OptiX $tag $name ..."
    Invoke-WebRequest "https://raw.githubusercontent.com/NVIDIA/optix-dev/$tag/include/$name" -OutFile $path
    if ((Get-FileHash $path).Hash -ne $files[$name]) { Remove-Item $path; throw "OptiX header hash mismatch: $name" }
}
Write-Host "OptiX headers: $dst"
