# Builds Layerbase Sequence Player (Release), the portable zip and, if Inno Setup 6/7 is installed, the installer.
#   .\build.ps1                 build exe + portable zip + installer (dist\)
#   .\build.ps1 -SkipInstaller  build exe only
#   .\build.ps1 -FFmpeg C:\path\ffmpeg.exe   replace the bundled FFmpeg (third_party\ffmpeg) first
#   .\build.ps1 -InstallerUrl <url>          also write the winget manifests (dist\winget)
param([switch]$SkipInstaller, [string]$FFmpeg, [string]$InstallerUrl)
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot

# vcpkg
$vcpkg = if ($env:VCPKG_ROOT) { $env:VCPKG_ROOT } else { Join-Path $env:USERPROFILE "vcpkg" }
if (-not (Test-Path "$vcpkg\vcpkg.exe")) {
    Write-Host "Cloning vcpkg into $vcpkg ..."
    git clone --depth 1 https://github.com/microsoft/vcpkg.git $vcpkg
    & "$vcpkg\bootstrap-vcpkg.bat" -disableMetrics
}
& "$vcpkg\vcpkg.exe" install "imgui[win32-binding,opengl3-binding]" openexr opencolorio tiff glew stb --triplet x64-windows-static --disable-metrics
if ($LASTEXITCODE) { throw "vcpkg install failed" }

# Newest Visual Studio with C++ tools (its bundled CMake knows its own generator)
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$cmake = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if (-not (Test-Path $cmake)) { $cmake = "cmake" }
$major = (& $vswhere -latest -products * -property catalog_productLineVersion)
$gen = switch ($major) { "2026" { "Visual Studio 18 2026" } "2022" { "Visual Studio 17 2022" } default { "Visual Studio 17 2022" } }

& $cmake -S $root -B "$root\build" -G $gen -A x64 "-DCMAKE_TOOLCHAIN_FILE=$vcpkg\scripts\buildsystems\vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows-static
if ($LASTEXITCODE) { throw "CMake configure failed" }
& $cmake --build "$root\build" --config Release
if ($LASTEXITCODE) { throw "Build failed" }
Write-Host "Built: $root\build\Release\SequencePlayer.exe"

# FFmpeg ships with the program (third_party\ffmpeg, see FFMPEG_README.txt).
if ($FFmpeg) { Copy-Item $FFmpeg "$root\third_party\ffmpeg\ffmpeg.exe" -Force; Write-Warning "FFmpeg replaced: update third_party\ffmpeg\FFMPEG_README.txt (version, source)" }
if (-not (Test-Path "$root\third_party\ffmpeg\ffmpeg.exe")) { & "$root\tools\get_ffmpeg.ps1" -Root $root }   # not in git (165 MB)
Copy-Item "$root\third_party\ffmpeg\ffmpeg.exe" "$root\build\Release\ffmpeg.exe" -Force   # used when running from build\Release

if (-not $SkipInstaller) {
    & "$root\tools\make_portable.ps1" -Root $root

    $iscc = @("7", "6") | ForEach-Object { "$env:LOCALAPPDATA\Programs\Inno Setup $_\ISCC.exe", "$env:ProgramFiles\Inno Setup $_\ISCC.exe", "${env:ProgramFiles(x86)}\Inno Setup $_\ISCC.exe" } | Where-Object { Test-Path $_ } | Select-Object -First 1
    if ($iscc) {
        & $iscc "$root\installer\SequencePlayer.iss"
        if ($LASTEXITCODE) {
            # Some antivirus products lock the output while ISCC patches resources: build in %TEMP% and copy.
            $tmpOut = Join-Path $env:TEMP "SequencePlayerSetup"
            & $iscc "/O$tmpOut" "$root\installer\SequencePlayer.iss"
            if ($LASTEXITCODE) { throw "Installer build failed" }
            New-Item -ItemType Directory -Force "$root\dist" | Out-Null
            Copy-Item "$tmpOut\*.exe" "$root\dist\" -Force
        }
        Write-Host "Installer: $root\dist"
        if ($InstallerUrl) { & "$root\tools\make_winget.ps1" -InstallerUrl $InstallerUrl -Root $root }
    } else {
        Write-Warning "Inno Setup not found - skipping installer (https://jrsoftware.org/isdl.php)"
    }
}
