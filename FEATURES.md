# Layerbase Sequence Player — Features and Benefits

**Free and open source (MIT) by [Layerbase Luxury Vision](https://layerbase.it)** · Windows 10/11 · English and Italian interface · [Download](https://github.com/polloviz/Layerbase-Sequence-Player/releases/latest) · [Source code](https://github.com/polloviz/Layerbase-Sequence-Player)

Layerbase Sequence Player is an image sequence player for 3D rendering, VFX and motion graphics artists.
It opens renders instantly (EXR, DPX, TIFF, PNG…) and shows them with correct color management (OCIO, ACES, AgX).
You can isolate objects with Cryptomatte and export client-ready movies, one at a time or in batch.
It is free and open source, including for commercial use.

---

## At a glance: why use it

| Task | Without Layerbase Sequence Player | With Layerbase Sequence Player |
|---|---|---|
| Review a render that just finished | Open a compositing or editing app, create a project, import the sequence | Double-click any frame: the whole sequence plays in under half a second |
| See the right colors (ACES, AgX) | Set up color management in your compositing app | ACES 2.0 config ready out of the box, built-in AgX, custom config.ocio in one click |
| Send a preview to a client | Render queue, codecs, export settings | Ctrl+E → H.264, H.265 or ProRes, with colors exactly as you see them |
| Convert dozens of shots | One export at a time | Batch-convert entire folders, subfolders included |
| Isolate an object (jewelry, product, character) | Compositing with Cryptomatte nodes | Click the object in the viewer → mask ready, exportable as ProRes 4444 alpha |
| Cost | Professional software licenses | Free, commercial work included |

---

## 1. Instant sequence opening

- **Double-click any frame** and the whole sequence in the folder loads, starting from that frame.
  Common naming schemes are recognized (`shot.1001.exr`, `render_0001.exr`, `render_1001_v02.exr`), and gaps in numbering are allowed.
- **Very fast startup**: the window appears in about 20 ms and the first EXR frame is on screen in about 0.3–0.4 seconds
  (measured on a workstation with 1080p renders). Decoding starts before the window is even ready.
- **Smart RAM cache**: upcoming frames are decoded in parallel on multiple cores while you watch;
  the timeline shows frames that are ready in blue. Memory use has a configurable limit.
- **No duplicate windows**: selecting several frames in File Explorer and pressing Enter opens a single window.
- **Drag and drop** files or folders onto the window.

- **Follows a render in progress**: new frames appear on the timeline as the renderer writes them, and re-rendered or
  unreadable frames are read again, while playback goes on.
- **Versions in one key**: when the name has a version (`shot_v002`, or a `v002` folder), Alt+Up / Alt+Down open the newer
  or older version on the same frame, with the same In/Out, colors, AOV stack and comparison.

**Why it helps:** checking a render becomes as quick as opening a photo. No projects to create, nothing to import.

## 2. Supported formats

EXR (half/float, all compressions, multi-layer and multi-part), DPX (8/10/12/16-bit), TIFF (8/16-bit, half/float),
PNG (8/16-bit), JPEG, TGA, BMP, HDR, PSD.

## 3. Professional color management (OpenColorIO 2.5 on the GPU)

- **ACES 2.0** (default) and **ACES 1.3** built in, plus every config shipped with OpenColorIO.
- **Custom config.ocio files**: load your studio or project config and use its color spaces, displays, views,
  looks and file rules. Recent configs stay in the menu.
- **AgX**:
  - installed **Blender** configs are found automatically;
  - on top of that, every config offers built-in *AgX*, *AgX Punchy* and *AgX Golden* views, so renders look the same as in Blender.
- **Quick controls**: Input (searchable), Display, View, Look, exposure (EV) and gamma, always visible at the top.
- **Automatic input selection**: EXR/HDR as scene-linear, integer formats as sRGB. Your last choice is remembered per config.
- The whole color transform runs on the **GPU**, so playback stays smooth even with ACES.
- **LUTs**: load a `.cube`, `.3dl`, `.csp`, `.clf` or any other LUT OpenColorIO reads, after the view (creative LUTs for Rec.709/sRGB)
  or before it in the grading space (ACEScct in the ACES configs). The LUT is part of playback, frame captures, export and batch conversion.
- **Alpha straight or premultiplied**: for sequences with an alpha channel you choose how the color is stored
  (premultiplied by default, as renderers write it; TIFF files that declare straight alpha are recognized),
  so edges and transparent areas look right.

**Why it helps:** you see the render exactly as it will look in the final pipeline, without opening compositing software
and without color mistakes in your previews.

## 4. Playback and review

- Default frame rate **30 fps**, presets from 12 to 120 fps or a custom value; actual fps shown during playback.
- Loop, play once, ping-pong, reverse playback, frame-by-frame stepping.
- **In/Out points** with the buttons next to the playback controls (or I / O). Clear them by clicking the In/Out label or pressing U.
- Zoom around the cursor with the mouse wheel, pan, fit/100%, fullscreen, hideable interface (Tab).
- **Playback resolution** 1:1, 1:2 or 1:4: heavy 4K+ renders and AOV stacks use 4 or 16 times less memory and play smoothly,
  while export always uses full resolution.
- Frames are sent to the graphics card in the background while the previous one is on screen, so large renders stay smooth.
- R, G, B, A and luminance channels. The pixel inspector shows the actual float values under the cursor.
- **Current frame in one click**: copy it to the clipboard (Ctrl+C) or save it as PNG, JPEG or 16-bit TIFF (Ctrl+S),
  at full resolution and exactly as you see it (color, LUT, exposure, channel, mask, AOV stack).
  Another button shows the frame's file selected in File Explorer.
- **No lost work**: opening another sequence by mistake asks for confirmation first.

### Compare versions (A/B)

- The **A/B** button compares the open sequence with another one or with one of its versions, frame by frame:
  **wipe** (drag the line), **side by side**, **difference** (black = identical, amplified to reveal small changes)
  or **toggle** between the two. W changes the mode, X swaps A and B.
- Both go through the same color pipeline, so only the render differs.

**Why it helps:** you see at once what changed between two versions, without a compositing app.

### Quality check (QC)

- **NaN, Inf and negative pixels** highlighted, with a count on the current frame: find broken pixels and fireflies before
  they reach the client.
- **False color** of the displayed brightness and **zebra** on clipped whites and crushed blacks.
- **Scopes**: waveform, histogram and vectorscope of the frame as you see it.
- **Guides**: aspect masks (2.39:1, 1.85:1, 16:9, 4:5, 1:1, 9:16 for social media), safe areas, rule of thirds, center cross.
- **Frame report**: missing frames (also marked on the timeline), empty or truncated files, frames that cannot be read;
  it can read every frame in the background, and the report can be copied for the render farm.
- **Metadata** (Ctrl+I): everything the renderer wrote in the file header (render settings, camera, Octane's JSON...).

Checks are shown only in the viewer, never in captures or exports.

**Why it helps:** technical checks that usually need a compositing app, one click away.

## 5. Multi-layer EXR

- The **Layer** menu lists every pass in the file (diffuse, specular, AOVs, Z, normals…) and the parts of multi-part EXRs.
- Switch layers with one click, for viewing or for export.
- The selected layer stays active when you open another shot that contains it.

**Why it helps:** you can check individual render passes without exporting them separately.

### AOV stack

- The **Stack** button opens a panel where you combine AOVs: passes of the open multi-layer EXR, or separate
  sequences with one pass each (select several at once, or drop them on the window while the panel is open).
- For each layer: visibility, input color space, **blend mode** (Normal, Add, Subtract, Multiply, Screen), opacity and exposure.
  Drag layers to change their order.
- Layers are blended in the scene-linear working space of the config, then the view (ACES, AgX…) is applied once:
  the result matches a compositing app, and it is what movie export writes.
- Passes stored in the same EXR are read in a single pass, and the stack costs nothing until you use it:
  opening a sequence stays just as fast.

**Why it helps:** you can rebuild the beauty from its passes, tweak each light or pass with its own exposure, and export
the result, without opening compositing software.

### Denoise

- The **Filters** button opens a panel with a **Denoise** filter for noisy renders: **Intel Open Image Denoise**
  (on the CPU, or on NVIDIA, AMD and Intel GPUs) or **NVIDIA OptiX** (NVIDIA GPUs).
- Each frame is denoised once and then plays from the cache: on a recent GPU a full HD frame takes a few milliseconds.
  Frame captures and movie exports use the denoised frames.
- When the EXR also holds the renderer's albedo and normal passes (Cycles *Denoising Albedo* / *Denoising Normal*
  and similar), they are used automatically as guides, which keeps textures and edges sharp.
- Open Image Denoise is downloaded only the first time it is used, after asking (57 MB, official release, verified);
  OptiX comes with the NVIDIA driver. Nothing is loaded until you turn the filter on: opening a sequence stays just as fast.
- **OptiX temporal** mode removes the flicker between frames: each frame is denoised together with the previous
  denoised one, which follows moving objects through the renderer's motion vectors (e.g. the Cycles *Vector* pass).
- In the AOV stack each layer can be denoised on its own, and **batch conversion** can denoise every sequence
  (also from the command line with `--denoise`).
- With A/B compare, B is left as it is: open the same shot as B for a before/after.

**Why it helps:** you can judge a quick low-sample render, or send a clean preview, without a compositing pass.

## 6. Cryptomatte

- Works with Cryptomatte from **Octane**, Arnold, Redshift, V-Ray, Blender and other renderers that follow the standard.
- **Five modes**:
  - **IDs**: a different color for each object or material;
  - **Overlay**: selected objects stay bright, everything else is dimmed;
  - **Masked**: only the selection stays visible;
  - **Matte**: the mask in black and white;
  - **Off**: the normal image.
- **One-click selection** of an object in the viewer, or from the searchable list of names.
- **Separate Cryptomatte sequence**: if your renderer saves Cryptomatte in files separate from the beauty (like Octane
  with a dedicated pass), open the beauty and choose the matte sequence with *Load Cryptomatte sequence…*.
  Frames are matched by number and the mask is applied to the beauty.
- The mask applies to playback and movie export.
- In **ProRes 4444** the mask can become the movie's **alpha channel**.

**Why it helps:** you can isolate a product, a piece of jewelry or a character in seconds and deliver a movie that is already cut out,
without going through compositing.

## 7. Movie export (Ctrl+E)

- **H.264** and **H.265** (MP4), including NVIDIA NVENC hardware encoding.
- **ProRes** 422 Proxy, LT, 422, HQ and **4444** with alpha (MOV), with straight color (what Premiere Pro and Final Cut expect)
  or premultiplied.
- Color is applied **exactly as you see it** on screen (config, view, look, LUT, exposure, gamma, layer, mask, AOV stack).
- H.265 and ProRes are encoded from 16-bit-per-channel data. Files are tagged with the correct primaries and transfer curve
  (sRGB, Rec.709, P3, Rec.2020, PQ), so players display them with the right colors.
- Full range or In/Out only, 100/50/25% scale, frame rate of your choice.
- **Framing**: crop to 2.39:1, 16:9, 4:5, 1:1, 9:16... (for example a vertical cut for social media) or keep the frame with black bars.
- **Burn-in**: shot name, frame number, timecode, date and your own text burned into the corners, for dailies and client reviews.

**Why it helps:** from render to client movie in a single step, with no color difference between the preview and the final file.

## 8. Batch conversion (Ctrl+B)

- Add several sequences, or an entire folder with all its subfolders: every sequence inside is found automatically.
- The same settings for all of them: format, quality, size, fps, layer, color management.
- Save next to each sequence or into a single folder, skipping movies that already exist.
- A table shows the status of each sequence, along with overall progress.
- Also available from the command line to automate your pipeline.

**Why it helps:** at the end of the day, you can convert every version of every shot in a single operation.

## 9. Windows integration

- The installer registers the supported formats: Layerbase Sequence Player appears in **"Open with"** and in **Default apps**.
  For extensions that have no associated program (often EXR and DPX) it becomes the default right away.
- Dedicated icons for the program and for associated files.
- Per-user installation without administrator rights; clean uninstall.
- **Portable version**: a zip to extract and run, with settings stored next to the program. Nothing is installed or written to the registry, ideal for studio machines where you cannot install software.

## 10. Lightweight and free

- Everything included: FFmpeg for movie export ships with the program, and no runtimes need to be installed.
- No account, no registration. The only network access is an optional daily update check that downloads a small version file:
  no personal data or usage statistics are sent, and you can turn it off in Settings.
- **Open source (MIT license)**: free for personal and commercial use; the source code is on GitHub.
- Built on industry-standard libraries: OpenColorIO and OpenEXR (Academy Software Foundation), FFmpeg for export.

---

## Main shortcuts

| Key | Action |
|---|---|
| Space | play / pause |
| J / K / L | reverse / stop / forward |
| ← / → | previous / next frame |
| I / O / U | set in / set out / clear |
| F, 1 | fit, 100% |
| R G B A Y, C | channels, back to RGB |
| Tab | hide interface |
| F11 / double-click | fullscreen |
| Ctrl+C / Ctrl+S | copy frame / save frame |
| Ctrl+Shift+R | show frame in File Explorer |
| Alt+↑ / Alt+↓ | newer / older version |
| W / X | compare mode / swap A and B |
| N / E / Z | NaN check / false color / zebra |
| H | scopes |
| Ctrl+I | metadata |
| Ctrl+E | export movie |
| Ctrl+B | batch convert |

---

*Layerbase Sequence Player — created by Layerbase Luxury Vision · https://layerbase.it*
