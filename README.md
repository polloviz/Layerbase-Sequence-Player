<p align="center"><img src="res/logo.png" width="128" alt="Layerbase Sequence Player"></p>

# Layerbase Sequence Player

**Fast image sequence player for Windows, with OpenColorIO (ACES 2.0 / 1.3, AgX), multi-layer EXR, Cryptomatte and movie export.**
Free and open source ([MIT](LICENSE.txt)) · by [Layerbase Luxury Vision](https://layerbase.it) · English and Italian interface

**[⬇ Download the latest release](https://github.com/polloviz/Layerbase-Sequence-Player/releases/latest)** — installer or portable zip · Windows 10/11 64-bit, OpenGL 4.1 GPU

What it does and why it's useful: **[FEATURES.md](FEATURES.md)**.

## Features

- **Formats:** EXR (half/float, all compressions, data/display window, multi-layer, multi-part), DPX (8/10/12/16-bit), TIFF (8/16-bit, half/float), PNG (8/16-bit), JPEG, TGA, BMP, HDR, PSD.
- **Sequences:** opening any file (`shot.1001.exr`, also `render_1001_v02.exr`) loads the whole sequence in the folder, starting from that frame. Gaps in numbering are allowed. Files or folders can be dropped onto the window. Selecting several frames in File Explorer and pressing Enter opens a single window.
- **Fast startup:** a single static exe. The first frame starts decoding *before* the window is created; a multi-threaded cache fills RAM in the background (timeline: blue = cached, red = error).
- **Color (OCIO 2.5, transform on the GPU):**
  - built-in configs: **ACES 2.0** Studio/CG (default), ACES 1.3 and all the others shipped with OCIO;
  - **custom config.ocio** (*Config → Load custom config.ocio…* or `--config`): uses its color spaces, displays, views, looks and file rules; recent configs stay in the menu;
  - config from the `$OCIO` environment variable;
  - **AgX:** installed **Blender** configs appear in the Config menu (original AgX with its looks, Filmic…); on top of that the View menu of *any* config offers *AgX / AgX Punchy / AgX Golden · built-in* (Blender/Filament formulation, SDR sRGB, Rec.1886 or Display P3 depending on the display);
  - Input (searchable, grouped by family), Display, View, Look, exposure (EV) and gamma;
  - automatic input: EXR/HDR → `scene_linear` (ACEScg), integer formats → sRGB; custom configs use their file rules. The last choice is remembered per config and per format class;
  - **OCIO** button to turn color management off.
- **Playback:** default **30 fps** (presets 12–120 or custom), loop / once / ping-pong, In/Out points (buttons beside the transport, I/O keys; click the In/Out label to clear), reverse playback, actual fps shown while playing.
- **Viewing:** fit/100%, wheel zoom around the cursor, pan, R/G/B/A/Luma channels, pixel inspector (float values under the cursor), fullscreen, hideable UI.
- **Movie export (Ctrl+E):** H.264 and H.265 (MP4, x264/x265 or NVIDIA NVENC), ProRes 422 Proxy/LT/422/HQ and 4444 (MOV). Color is applied exactly as displayed (input → display/view, look, exposure, gamma, channel) on the GPU; H.265 and ProRes are fed 16 bits per channel. Files are tagged with the display primaries/transfer (sRGB, Rec.1886, P3, Rec.2020, PQ). Full range or In/Out, 100/50/25% scale, frame rate. **FFmpeg is included**; another build can be chosen in the export dialog (also searched in PATH, `C:\FFMPEG\bin`, winget, choco).
- **Multi-layer EXR:** layers and multi-part files are listed in the *Layer* menu (bottom bar); the selected layer is shown and exported (XYZ vectors and single channels such as Z are shown as RGB / gray). The layer stays selected when opening another shot that contains it.
- **Cryptomatte:** the *Cryptomatte* button (bottom bar) opens the panel: layer (CryptoObject/Material/Asset), *IDs* (colors per object), *Overlay*, *Masked* (only the selection, in scene-linear before the view), *Matte* (black and white mask). Select objects by clicking in the viewer or from the manifest list (searchable). The mask applies to playback and export; in ProRes 4444 it can become the alpha channel. Channel names are matched case-insensitively (Octane writes `.r/.g/.b/.a`). A Cryptomatte-only file opens in IDs mode.
  - **External Cryptomatte sequence:** with a sequence open, *Load Cryptomatte sequence…* (panel or menu) masks it with the Cryptomatte of another sequence (e.g. Octane's `cm-*` pass). Frames are matched by number (by position if the numbering differs); a different resolution is scaled. Works with a beauty in any format. Opening another shot removes the external matte, so it does not apply to batch conversion.
- **Batch conversion (Ctrl+B):** add sequences (multi-select), a folder (with subfolders) or drop several files/folders on the window; every sequence found is converted with the same settings (format, quality, size, fps, layer, color). Output next to each sequence or into one folder, skipping existing files, with per-sequence status.
- **Windows integration:** the installer registers the formats → the player appears in **"Open with"** and in **Default apps**; for extensions without an associated program (often `.exr`, `.dpx`) it becomes the default. Windows does not let programs make themselves the default for extensions that are already associated: *Set as default app…* (Settings) opens the right Windows page.
- **Portable mode:** `portable.txt` next to the exe keeps settings in a `data` folder beside it, with nothing written to the registry.
- **Update check:** at most once a day, `installer/version.json` is read from this repository; a newer version shows a dismissible banner. It can be turned off in Settings; no personal data or statistics are sent.

## Shortcuts

| Key | Action |
|---|---|
| Space | play / pause |
| J / K / L | reverse / stop / forward |
| ← / → (Shift = 10) | previous / next frame |
| Home / End | first / last frame |
| I / O / U | set in / set out / clear |
| F, 1, 2, 3 | fit, 100%, 200%, 50% |
| Wheel, drag (left/middle) | zoom, pan |
| R G B A Y, C | channels, back to RGB |
| `-` `+` (or `[` `]`), Backspace | exposure ±0.5 EV, reset exposure/gamma |
| Tab | hide interface |
| F11 / Enter / double-click | fullscreen |
| Ctrl+O / Ctrl+Shift+O | open file / folder |
| Ctrl+E | export movie |
| Ctrl+B | batch convert |
| click in viewer (Cryptomatte on) | add / remove the object from the mask |

## Command line

```
SequencePlayer.exe [--fps 24] [--play] [--config C:\path\config.ocio | ocio://studio-config-latest]
                   [--display "sRGB - Display"] [--view "ACES 2.0 - SDR 100 nits (Rec.709)"] [file or folder]
SequencePlayer.exe shot.1001.exr --export shot.mov [--codec h264|h265|prores-proxy|prores-lt|prores|prores-hq|prores-4444] [--nvenc] [--alpha]
                   exports without interaction, then exits (exit code 0 = ok)
SequencePlayer.exe --batch D:\renders [--out-dir D:\movies] [--codec h264] [--nvenc] [--overwrite]
                   converts every sequence in the folder and subfolders, then exits
EXR options:       [--layer diffuse] [--crypto-layer CryptoObject] [--matte ids|overlay|masked|matte] [--select ball,floor]
                   [--crypto-seq D:\render\cm\shot_cm_0000.exr]   mask from an external Cryptomatte sequence
Command-line options never change the saved settings.
SequencePlayer.exe --register      registers the formats (per user, no admin)
SequencePlayer.exe --unregister    removes the registration
```

Debug environment variables: `SP_LOG=1` writes `%TEMP%\SequencePlayer.log` with startup timings; `SP_DUMP=1` (or `=<ms>`) saves a frame of the window to `%TEMP%\SequencePlayer_dump.ppm`; `SP_TEST_OPEN=about|settings|batch|crypto` opens a dialog at startup; `SP_UPDATE_URL=<url>` uses another update manifest.

## Building

Requirements: Visual Studio 2022/2026 with C++, Git. The installer needs Inno Setup 6 or 7.

```powershell
.\build.ps1                               # vcpkg dependencies + exe + portable zip + installer (dist\)
.\build.ps1 -SkipInstaller                # exe only (build\Release\SequencePlayer.exe)
.\build.ps1 -FFmpeg C:\path\ffmpeg.exe    # replace the bundled FFmpeg (third_party\ffmpeg)
.\build.ps1 -InstallerUrl <url>           # also write the winget manifests (dist\winget)
```

Dependencies (OpenColorIO, OpenEXR, libtiff, Dear ImGui, GLEW, stb) are built by vcpkg with the `x64-windows-static` triplet.
vcpkg and the project must use the same MSVC toolset: `build.ps1` uses the newest Visual Studio installed.

The display name is *Layerbase Sequence Player*; the exe (`SequencePlayer.exe`), ProgIDs and registry keys keep their original identifiers so that updates replace version 1.1 in place.

## Releasing

Output in `dist\`:

| File | Content |
|---|---|
| `LayerbaseSequencePlayer-<ver>-Setup.exe` | installer (per user or all users), with FFmpeg |
| `LayerbaseSequencePlayer-<ver>-Portable.zip` | folder to extract; `portable.txt` keeps settings in `data\` |
| `winget\manifests\...` | winget manifests (with `-InstallerUrl`) |

1. Bump the version in `CMakeLists.txt`, `res/app.rc` and `installer/SequencePlayer.iss`, then run `.\build.ps1`.
2. Publish a GitHub release `v<ver>` with the installer and the portable zip.
3. Update `installer/version.json` (`version`, `url`, `notes_en`, `notes_it`) and push it to `main`: installed copies will show the update banner.
4. winget: `.\tools\make_winget.ps1 -InstallerUrl https://github.com/polloviz/Layerbase-Sequence-Player/releases/download/v<ver>/LayerbaseSequencePlayer-<ver>-Setup.exe`, check with `winget validate` and `winget install --manifest`, then open a pull request on https://github.com/microsoft/winget-pkgs. Signing the installer changes its hash: regenerate the manifests afterwards.

- **FFmpeg** (`third_party/ffmpeg`): unmodified BtbN n8.1.3 GPL build, shipped with `FFMPEG_LICENSE.txt` and `FFMPEG_README.txt` (version, source code, source offer). `ffmpeg.exe` is not in the repository (165 MB, over GitHub's file limit): `build.ps1` downloads it with `tools/get_ffmpeg.ps1` (pinned version, verified hash). When updating FFmpeg, update `FFMPEG_README.txt` and the script.
- **Press and community texts:** `press/press-release.md` (EN/IT + editor email), `press/community-posts.md` (forums, Reddit, LinkedIn, social, video script).

## Source layout

| File | Role |
|---|---|
| `src/App.cpp` | Win32 window, OpenGL 4.1 context, main loop, playback, input |
| `src/UI.cpp` | interface (color bar, timeline, transport, settings) |
| `src/ColorManager.cpp` | OCIO configs, color space/display/view/look lists, processors |
| `src/GLViewer.cpp` | OCIO-generated GLSL shader, LUT textures, image drawing |
| `src/ColorAgx.cpp` | built-in AgX (native OCIO transforms), Blender config discovery |
| `src/Export.cpp`, `src/ExportUI.cpp` | movie export through FFmpeg (pipe), dialog and progress |
| `src/ExrLayers.cpp`, `src/CryptoUI.cpp` | multi-layer/multi-part EXR, Cryptomatte decoding, panel |
| `src/BatchUI.cpp` | batch conversion (recursive search, queue, status) |
| `src/AboutUI.cpp` | About dialog (credits, licenses) |
| `src/UpdateCheck.cpp`, `src/UpdateUI.cpp` | optional update check (WinHTTP) and banner |
| `src/FrameCache.cpp` | multi-threaded decoding with a memory-budgeted cache |
| `src/ImageIO.cpp` | EXR, DPX, TIFF and stb readers |
| `src/Sequence.cpp` | sequence detection from file names |
| `src/Platform.cpp` | Windows registry ("Open with"), dialogs, HTTP, utilities |
| `src/I18n.h` | all EN/IT strings |
| `installer/SequencePlayer.iss` | Inno Setup installer (EN/IT) |
| `tools/make_icon.py`, `tools/make_notices.py` | icons from `res/*_icon.png`, third-party notices from vcpkg |
| `tools/make_portable.ps1`, `tools/make_winget.ps1`, `tools/get_ffmpeg.ps1` | portable zip, winget manifests, FFmpeg download |

## License

[MIT](LICENSE.txt) © 2026 Layerbase Luxury Vision. Third-party components keep their own licenses: see [res/THIRD_PARTY_NOTICES.txt](res/THIRD_PARTY_NOTICES.txt) and [third_party/ffmpeg/FFMPEG_README.txt](third_party/ffmpeg/FFMPEG_README.txt) (FFmpeg is GPLv3 and runs as a separate program).
