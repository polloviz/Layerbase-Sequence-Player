# Layerbase Sequence Player

**Freeware di [Layerbase Luxury Vision](https://layerbase.it)** — gratuito per uso personale e commerciale (vedi [LICENSE.txt](LICENSE.txt) / [LICENSE_IT.txt](LICENSE_IT.txt); componenti di terze parti in [res/THIRD_PARTY_NOTICES.txt](res/THIRD_PARTY_NOTICES.txt)).

Player di sequenze di immagini per Windows, veloce e minimale, con gestione colore OpenColorIO (ACES 1.3 e ACES 2.0).
Il nome visualizzato è *Layerbase Sequence Player*; exe (`SequencePlayer.exe`), ProgID e chiavi di registro mantengono gli identificativi originali, così gli aggiornamenti sostituiscono la 1.1.
Interfaccia in **inglese** e **italiano** (rilevata dalla lingua di Windows, modificabile nelle Impostazioni).

## Funzioni

- **Formati:** EXR (half/float, tutte le compressioni, data/display window), DPX (8/10/12/16 bit), TIFF (8/16 bit, half/float), PNG (8/16 bit), JPEG, TGA, BMP, HDR, PSD.
- **Sequenze:** aprendo un file qualsiasi (`shot.1001.exr`, anche `render_1001_v02.exr`) viene caricata l'intera sequenza della cartella, partendo dal frame aperto. I buchi nella numerazione sono ammessi. Si può trascinare nella finestra un file o una cartella. Selezionando più frame in Esplora risorse e premendo Invio si apre una sola finestra.
- **Avvio rapido:** exe unico statico. La decodifica del primo frame parte *prima* della creazione della finestra; la cache multi-thread riempie la RAM in background (timeline: blu = in cache, rosso = errore).
- **Colore (OCIO 2.5, trasformazione su GPU):**
  - config integrate: **ACES 2.0** Studio/CG (predefinita), ACES 1.3 e tutte le altre fornite da OCIO;
  - **config.ocio personalizzate** (menu *Config → Carica config.ocio personalizzata…*, oppure `--config`): vengono usati colorspace, display, view, look e file rules del config; le config recenti restano nel menu;
  - config dalla variabile d'ambiente `$OCIO`;
  - **AgX:** le config di **Blender** installate compaiono nel menu Config (AgX originale con i suoi look, Filmic, ecc.); in più il menu View di *qualsiasi* config offre *AgX / AgX Punchy / AgX Golden · built-in* (formulazione Blender/Filament, SDR sRGB, Rec.1886 o Display P3 in base al display);
  - Input (con ricerca e raggruppamento per famiglia), Display, View, Look, esposizione (EV) e gamma;
  - input automatico: EXR/HDR → `scene_linear` (ACEScg), formati interi → sRGB; per le config custom valgono le file rules. L'ultima scelta viene ricordata per config e per classe di formato;
  - pulsante **OCIO** per disattivare la gestione colore.
- **Riproduzione:** frame rate predefinito **30 fps** (preset 12–120 o valore personalizzato), loop / una volta / ping-pong, punti in/out (pulsanti ai lati dei controlli di riproduzione, tasti I/O; clic sull'etichetta In/Out per azzerare), riproduzione al contrario, fps effettivi mostrati durante il play.
- **Visualizzazione:** adatta/100%, zoom con rotella sul cursore, pan, canali R/G/B/A/Luma, ispettore pixel (valori float sotto il cursore), schermo intero, UI nascondibile.
- **Export filmati (Ctrl+E):** H.264 e H.265 (MP4, software x264/x265 o NVIDIA NVENC), ProRes 422 Proxy/LT/422/HQ e 4444 (MOV). Il colore viene applicato esattamente come lo vedi (input → display/view, look, esposizione, gamma, canale), su GPU; H.265 e ProRes partono da 16 bit per canale. Il file viene marcato con primarie/transfer del display (sRGB, Rec.1886, P3, Rec.2020, PQ). Intervallo completo o in/out, scala 100/50/25%, frame rate. Usa **FFmpeg** (cercato accanto all'exe, nel PATH, in `C:\FFMPEG\bin`, winget, choco, oppure indicato a mano).
- **EXR multi-layer:** layer e file multi-part vengono elencati nel menu *Layer* (barra in basso); si visualizza ed esporta il layer scelto (vettori XYZ e canali singoli come Z vengono mostrati in RGB / grigio). Il layer scelto resta attivo passando a un altro shot che lo contiene.
- **Cryptomatte:** pulsante *Cryptomatte* (barra in basso) apre il pannello: layer (CryptoObject/Material/Asset), modalità *ID* (colori per oggetto), *Evidenzia*, *Mascherato* (solo gli oggetti selezionati, in scene-linear prima della view), *Matte* (maschera in bianco e nero). Si selezionano oggetti cliccando nel viewer o dall'elenco del manifest (con ricerca). Maschera applicata a riproduzione ed export; in ProRes 4444 può diventare il canale alpha. Canali letti senza distinzione maiuscole/minuscole (Octane scrive `.r/.g/.b/.a`). Un file con solo Cryptomatte si apre direttamente in modalità ID.
  - **Sequenza Cryptomatte esterna:** con una sequenza aperta, *Carica sequenza Cryptomatte…* (pannello o menu) usa i Cryptomatte di un'altra sequenza (es. pass `cm-*` di Octane) per mascherarla. Frame abbinati per numero (per posizione se le numerazioni non coincidono), risoluzione diversa scalata. Funziona con beauty di qualsiasi formato. Aprendo un altro shot il matte esterno viene rimosso (quindi non si applica al batch).
- **Conversione batch (Ctrl+B):** aggiungi sequenze (selezione multipla), una cartella (con sottocartelle) o trascina più file/cartelle nella finestra; tutte le sequenze trovate vengono convertite con le stesse impostazioni (formato, qualità, dimensione, fps, layer, colore). Destinazione accanto a ogni sequenza o in una cartella unica, con salto dei file già esistenti e stato per ogni sequenza.
- **Integrazione Windows:** l'installer registra i formati → Layerbase Sequence Player compare in **"Apri con"** e nelle **App predefinite** di Windows; per le estensioni senza programma associato (es. `.exr`, `.dpx` su molti PC) diventa direttamente il predefinito. Windows non permette ai programmi di impostarsi da soli come predefiniti per estensioni già associate: il pulsante *Imposta come app predefinita…* (Impostazioni) apre la pagina di Windows già filtrata.

## Scorciatoie

| Tasto | Azione |
|---|---|
| Spazio | play / pausa |
| J / K / L | indietro / stop / avanti |
| ← / → (Shift = 10) | frame precedente / successivo |
| Home / Fine | primo / ultimo frame |
| I / O / U | punto in / out / azzera |
| F, 1, 2, 3 | adatta, 100%, 200%, 50% |
| Rotella, trascina (sinistro/centrale) | zoom, pan |
| R G B A Y, C | canali, torna a RGB |
| `-` `+` (o `[` `]`) , Backspace | esposizione ±0.5 EV, reset esposizione/gamma |
| Tab | nascondi interfaccia |
| F11 / Invio / doppio clic | schermo intero |
| Ctrl+O / Ctrl+Shift+O | apri file / cartella |
| Ctrl+E | esporta filmato |
| Ctrl+B | conversione batch |
| clic nel viewer (Cryptomatte attivo) | aggiunge / toglie l'oggetto dalla maschera |

## Riga di comando

```
SequencePlayer.exe [--fps 24] [--play] [--config C:\path\config.ocio | ocio://studio-config-latest]
                   [--display "sRGB - Display"] [--view "ACES 2.0 - SDR 100 nits (Rec.709)"] [file o cartella]
SequencePlayer.exe shot.1001.exr --export shot.mov [--codec h264|h265|prores-proxy|prores-lt|prores|prores-hq|prores-4444] [--nvenc] [--alpha]
                   esporta senza interazione e chiude (exit code 0 = ok)
SequencePlayer.exe --batch D:\renders [--out-dir D:\movies] [--codec h264] [--nvenc] [--overwrite]
                   converte tutte le sequenze della cartella e sottocartelle e chiude
Opzioni EXR:       [--layer diffuse] [--crypto-layer CryptoObject] [--matte ids|overlay|masked|matte] [--select ball,floor]
                   [--crypto-seq D:\render\cm\shot_cm_0000.exr]   matte da una sequenza Cryptomatte esterna
Le opzioni da riga di comando non modificano le impostazioni salvate.
SequencePlayer.exe --register      registra i formati (per-utente, senza admin)
SequencePlayer.exe --unregister    rimuove la registrazione
```

Debug: con la variabile d'ambiente `SP_LOG=1` viene scritto `%TEMP%\SequencePlayer.log` con i tempi di avvio.

Elenco completo delle funzioni e dei vantaggi per gli utenti: [FEATURES.md](FEATURES.md).

## Compilazione

Requisiti: Visual Studio 2022/2026 con C++, Git. L'installer richiede Inno Setup 6 o 7.

```powershell
.\build.ps1                 # dipendenze vcpkg + exe + zip portable + installer (dist\)
.\build.ps1 -SkipInstaller  # solo exe (build\Release\SequencePlayer.exe)
.\build.ps1 -FFmpeg C:\path\ffmpeg.exe      # sostituisce l'FFmpeg incluso (third_party\ffmpeg)
.\build.ps1 -InstallerUrl <url>          # genera anche i manifest winget (dist\winget)
```

Le dipendenze (OpenColorIO, OpenEXR, libtiff, Dear ImGui, GLEW, stb) vengono compilate da vcpkg con triplet `x64-windows-static`.
vcpkg e il progetto devono usare lo stesso toolset MSVC: `build.ps1` usa l'ultima versione di Visual Studio installata.

## Distribuzione

Output in `dist\`:

| File | Contenuto |
|---|---|
| `LayerbaseSequencePlayer-<ver>-Setup.exe` | installer (per utente o per tutti), con FFmpeg |
| `LayerbaseSequencePlayer-<ver>-Portable.zip` | cartella da estrarre: `portable.txt` accanto all'exe salva le impostazioni in `data\` e non tocca il registro |
| `winget\manifests\...` | manifest winget (con `-InstallerUrl`) |

- **FFmpeg** (`third_party\ffmpeg`): build BtbN n8.1.3 GPL, copiata invariata con `FFMPEG_LICENSE.txt` e `FFMPEG_README.txt` (versione, sorgenti, offerta dei sorgenti). `ffmpeg.exe` non è nel repository (165 MB, oltre il limite di GitHub): `build.ps1` lo scarica con `tools\get_ffmpeg.ps1` (versione fissata, hash verificato). Aggiornando FFmpeg, aggiorna `FFMPEG_README.txt` e lo script.
- **Aggiornamenti:** il programma legge `https://layerbase.it/sequence-player/version.json` (al massimo una volta al giorno, disattivabile nelle Impostazioni). Per ogni versione carica sul sito `installer\version.json` aggiornato (`version`, `url`, `notes_en`, `notes_it`). Finché il file non esiste il controllo fallisce in silenzio. Test: `SP_UPDATE_URL=http://127.0.0.1:8765/version.json`.
- **winget:** carica l'installer definitivo (firmato) dove resterà (es. GitHub Releases), poi `.\tools\make_winget.ps1 -InstallerUrl <url>`; verifica con `winget validate` e `winget install --manifest`, poi proponi la cartella in una pull request su https://github.com/microsoft/winget-pkgs. Firmare l'installer dopo aver generato i manifest cambia l'hash: rigenerali.
- **Comunicati e post:** `press\press-release.md` (EN/IT + email per le redazioni), `press\community-posts.md` (forum, Reddit, LinkedIn, social, script del video).
## Struttura

| File | Ruolo |
|---|---|
| `src/App.cpp` | finestra Win32, contesto OpenGL 4.1, loop, riproduzione, input |
| `src/UI.cpp` | interfaccia (barra colore, timeline, trasporto, impostazioni) |
| `src/ColorManager.cpp` | config OCIO, liste colorspace/display/view/look, processor |
| `src/GLViewer.cpp` | shader GLSL generato da OCIO, texture LUT, disegno immagine |
| `src/ColorAgx.cpp` | AgX integrato (trasformazioni OCIO native), rilevamento config Blender |
| `src/Export.cpp`, `src/ExportUI.cpp` | export filmati via FFmpeg (pipe), dialogo e avanzamento |
| `src/ExrLayers.cpp`, `src/CryptoUI.cpp` | EXR multi-layer/multi-part, decodifica Cryptomatte, pannello |
| `src/BatchUI.cpp` | conversione batch (ricerca ricorsiva, coda, stato) |
| `src/AboutUI.cpp` | finestra About (Layerbase Luxury Vision, licenze) |
| `tools/make_icon.py`, `tools/make_notices.py` | icone da `res/*_icon.png`, avvisi di terze parti da vcpkg |
| `src/FrameCache.cpp` | decodifica multi-thread con cache a budget di memoria |
| `src/ImageIO.cpp` | lettori EXR, DPX, TIFF, stb |
| `src/Sequence.cpp` | rilevamento sequenze dal nome file |
| `src/Platform.cpp` | registro di Windows ("Apri con"), dialoghi, utilità |
| `src/I18n.h` | tutte le stringhe EN/IT |
| `src/UpdateCheck.cpp`, `src/UpdateUI.cpp` | controllo aggiornamenti facoltativo (WinHTTP) e avviso |
| `installer/SequencePlayer.iss` | installer Inno Setup (EN/IT) |
| `tools/make_portable.ps1`, `tools/make_winget.ps1` | zip portable, manifest winget |
