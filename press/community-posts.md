# Community posts — Layerbase Sequence Player 1.2

Replace `[VIDEO URL]`. Post from a personal account, stay in the thread to answer questions,
and read each community's rules on self-promotion first (some subreddits allow tools only in weekly threads or with a flair).
Each post leads with a problem that community has; do not paste the same text everywhere.

---

## OTOY forum (Octane) / Octane Facebook groups

**Title:** Free tool: mask Octane renders with the separate Cryptomatte pass and export ProRes 4444 with alpha

Octane saves Cryptomatte as its own pass (a separate EXR sequence, with lowercase `.r/.g/.b/.a` channels), and
many viewers either don't read it or can't apply it to the beauty. We made a free Windows player that does:

1. open the beauty sequence (double-click any frame);
2. *Load Cryptomatte sequence…* and pick any frame of the Cryptomatte pass: frames are matched by number;
3. click the objects or materials you want in the viewer (or pick them from the manifest list);
4. export ProRes 4444 with the selection as the alpha channel, or H.264 with the rest masked out.

It also plays EXR sequences with ACES 2.0 / AgX / custom OCIO configs, shows multi-layer passes and batch-converts folders.
Free and open source (MIT), commercial use included: https://github.com/polloviz/Layerbase-Sequence-Player/releases/latest · 1-minute demo: [VIDEO URL]

Feedback from Octane users is very welcome, especially Cryptomatte setups we haven't tested.

---

## r/Cinema4D · Core4D · C4D Cafe

**Title:** I made a free EXR sequence player for checking C4D renders (ACES 2.0, OCIO, ProRes export)

Checking a finished render sequence from C4D usually means opening another app and creating a project.
Layerbase Sequence Player opens the whole sequence from a double-click on any frame (first frame in about 0.3 s here),
shows it through OCIO with ACES 2.0 or your own config, and exports H.264/H.265/ProRes with the same colors you see.
Multi-layer EXR, Cryptomatte (Octane, Redshift, Arnold…) and batch conversion of whole folders are in too.

Free, also for commercial work, Windows only: https://github.com/polloviz/Layerbase-Sequence-Player/releases/latest
Happy to hear what's missing for your workflow.

---

## r/blender · BlenderArtists (Artwork/Released Scripts and Themes → or "Other software") · BlenderNation tip

**Title:** Free image sequence player with Blender's AgX built in (and your installed Blender configs detected)

If you render EXR sequences from Blender, you've probably noticed most players don't show them like Blender does.
This one finds your installed Blender OCIO configs automatically (AgX with its looks, Filmic…) and also has built-in
AgX / AgX Punchy / AgX Golden views for any config. It plays full sequences from RAM, shows every render pass of multi-layer
EXRs, isolates objects with Cryptomatte and exports H.264 or ProRes with the colors you see.

Windows, free and open source (MIT, commercial use OK): https://github.com/polloviz/Layerbase-Sequence-Player/releases/latest · demo: [VIDEO URL]

---

## r/vfx · r/cgi

**Title:** Free Windows sequence player: EXR multi-part, OCIO/ACES 2.0 on GPU, Cryptomatte, ProRes 4444 alpha, batch

Built this for our own reviews at a product-viz studio and decided to release it as open source (MIT):

- instant start (sequence detection from any frame, multi-threaded RAM cache);
- OCIO 2.5 on the GPU: ACES 2.0/1.3, custom configs with file rules and looks, EV/gamma, pixel inspector with float values;
- EXR layers/parts, Cryptomatte IDs/overlay/masked/matte, external Cryptomatte sequences;
- export H.264/H.265 (x264/x265/NVENC) and ProRes Proxy→4444, 16-bit source for H.265/ProRes, color tags written;
- batch conversion of folder trees, command line for pipeline use.

https://github.com/polloviz/Layerbase-Sequence-Player/releases/latest. Not trying to replace RV or Nuke: it's the quick "double-click and check" tool.
Bug reports and feature requests welcome.

---

## LinkedIn (English)

We've released **Layerbase Sequence Player**, the image sequence player we use every day at Layerbase Luxury Vision,
as free software for the 3D and VFX community.

Double-click any frame of a render and the whole sequence plays, with ACES 2.0 or AgX color management.
Click a jewel in the viewer to isolate it with Cryptomatte and export a ProRes 4444 movie with alpha, ready for the edit.

Free, also for commercial use. Download: https://github.com/polloviz/Layerbase-Sequence-Player/releases/latest

#3D #VFX #ACES #OpenEXR #Cryptomatte #ProductVisualization #Octane #Blender #Cinema4D

## LinkedIn (italiano)

Abbiamo pubblicato **Layerbase Sequence Player**, il player di sequenze che usiamo ogni giorno in Layerbase Luxury Vision,
come software gratuito per la community 3D e VFX.

Doppio clic su un frame qualsiasi del render e parte l'intera sequenza, con gestione colore ACES 2.0 o AgX.
Clic su un gioiello nel viewer per isolarlo con Cryptomatte ed esportare un filmato ProRes 4444 con alpha, pronto per il montaggio.

Gratuito anche per uso commerciale. Download: https://github.com/polloviz/Layerbase-Sequence-Player/releases/latest

#3D #VFX #ACES #Cryptomatte #ProductVisualization #Octane #Blender #Cinema4D

---

## Instagram / Behance / X / Bluesky (with the demo video)

**EN:** From render to client movie in one click. Layerbase Sequence Player: EXR sequences, ACES 2.0 & AgX,
Cryptomatte masking, ProRes 4444 with alpha. Free for commercial use. Link in bio.
#3dart #vfx #octanerender #blender3d #c4d #productvisualization #jewelryrendering

**IT:** Dal render al filmato per il cliente con un clic. Layerbase Sequence Player: sequenze EXR, ACES 2.0 e AgX,
maschere Cryptomatte, ProRes 4444 con alpha. Gratuito anche per uso commerciale. Link in bio.

---

## Video script (60 seconds)

1. (0–8 s) Double-click an EXR frame in File Explorer → the sequence plays at once. Caption: "Double-click. Playing."
2. (8–20 s) Switch the view between ACES 2.0 and AgX, change exposure. Caption: "ACES 2.0 · AgX · your OCIO config."
3. (20–30 s) Layer menu: beauty → specular → diffuse. Caption: "Every EXR pass."
4. (30–45 s) Load the Cryptomatte sequence, click the jewel, switch to Masked. Caption: "Click to isolate. Octane Cryptomatte too."
5. (45–55 s) Ctrl+E → ProRes 4444 with alpha → the movie on a timeline over a background. Caption: "ProRes 4444 with alpha."
6. (55–60 s) Logo and URL. Caption: "Free. Also for commercial use. layerbase.it"
