#pragma once

enum class Lang { English = 0, Italian = 1 };

// X(id, english, italian)
#define STRING_TABLE(X) \
    X(Open,              "Open...",                               "Apri...") \
    X(OpenFolder,        "Open folder...",                        "Apri cartella...") \
    X(Recent,            "Recent",                                "Recenti") \
    X(NoRecent,          "No recent files",                       "Nessun file recente") \
    X(Settings,          "Settings",                              "Impostazioni") \
    X(About,             "About",                                 "Informazioni") \
    X(Exit,              "Exit",                                  "Esci") \
    X(DropHint,          "Drop an image sequence here",           "Trascina qui una sequenza di immagini") \
    X(DropHint2,         "or press Ctrl+O to open a file",        "oppure premi Ctrl+O per aprire un file") \
    X(Config,            "Config",                                "Config") \
    X(BuiltinConfigs,    "Built-in configs",                      "Config integrate") \
    X(LoadCustomConfig,  "Load custom config.ocio...",            "Carica config.ocio personalizzata...") \
    X(RecentConfigs,     "Recent custom configs",                 "Config personalizzate recenti") \
    X(EnvConfig,         "From $OCIO environment variable",       "Da variabile d'ambiente $OCIO") \
    X(Input,             "Input",                                 "Input") \
    X(Display,           "Display",                               "Display") \
    X(View,              "View",                                  "Vista") \
    X(Look,              "Look",                                  "Look") \
    X(NoLook,            "None",                                  "Nessuno") \
    X(Exposure,          "Exposure",                              "Esposizione") \
    X(Gamma,             "Gamma",                                 "Gamma") \
    X(ColorManaged,      "Color management",                      "Gestione colore") \
    X(Search,            "Search...",                             "Cerca...") \
    X(AutoDetect,        "Auto (from file type)",                 "Auto (da tipo file)") \
    X(Fps,               "fps",                                   "fps") \
    X(FrameRate,         "Frame rate",                            "Frame rate") \
    X(Custom,            "Custom",                                "Personalizzato") \
    X(Loop,              "Loop",                                  "Loop") \
    X(Once,              "Play once",                             "Riproduci una volta") \
    X(PingPong,          "Ping-pong",                             "Ping-pong") \
    X(Play,              "Play",                                  "Riproduci") \
    X(Pause,             "Pause",                                 "Pausa") \
    X(PlayReverse,       "Play reverse",                          "Riproduci al contrario") \
    X(StepBack,          "Previous frame",                        "Frame precedente") \
    X(StepForward,       "Next frame",                            "Frame successivo") \
    X(FirstFrame,        "First frame",                           "Primo frame") \
    X(LastFrame,         "Last frame",                            "Ultimo frame") \
    X(Fit,               "Fit",                                   "Adatta") \
    X(Channels,          "Channels",                              "Canali") \
    X(Language,          "Language",                              "Lingua") \
    X(LangAuto,          "Automatic (system)",                    "Automatica (sistema)") \
    X(DefaultFps,        "Default frame rate",                    "Frame rate predefinito") \
    X(CacheMemory,       "Cache memory",                          "Memoria cache") \
    X(FileAssoc,         "File associations",                     "Associazioni file") \
    X(Register,          "Register file types",                   "Registra tipi di file") \
    X(Unregister,        "Remove registration",                   "Rimuovi registrazione") \
    X(DefaultApps,       "Set as default app...",                 "Imposta come app predefinita...") \
    X(Registered,        "Registered: Layerbase Sequence Player appears in \"Open with\".", "Registrato: Layerbase Sequence Player compare in \"Apri con\".") \
    X(NotRegistered,     "Not registered.",                       "Non registrato.") \
    X(Close,             "Close",                                 "Chiudi") \
    X(Frame,             "Frame",                                 "Frame") \
    X(Frames,            "frames",                                "frame") \
    X(Cached,            "cached",                                "in cache") \
    X(LoadError,         "Cannot load frame",                     "Impossibile caricare il frame") \
    X(ConfigError,       "OCIO error",                            "Errore OCIO") \
    X(Shortcuts,         "Shortcuts",                             "Scorciatoie") \
    X(ShortcutsText,     "Space  play/pause\nJ / K / L  reverse / stop / forward\nLeft / Right  step\nHome / End  first / last\nI / O  in / out point,  U  clear\nF  fit,  1  100%,  wheel  zoom,  middle drag  pan\nR G B A  channels,  C  RGB\n[ ] or - +  exposure,  Backspace  reset\nTab  hide UI,  F11 / double click  fullscreen\nCtrl+C  copy frame,  Ctrl+S  save frame,  Ctrl+Shift+R  show in Explorer\nCtrl+E  export movie,  Ctrl+B  batch convert\nAlt+Up / Alt+Down  newer / older version\nW  compare mode,  X  swap A/B\nN  NaN check,  E  false color,  Z  zebra,  H  scopes,  Ctrl+I  metadata", \
                                                                  "Spazio  play/pausa\nJ / K / L  indietro / stop / avanti\nSinistra / Destra  frame singolo\nHome / Fine  primo / ultimo\nI / O  punto in / out,  U  azzera\nF  adatta,  1  100%,  rotella  zoom,  tasto centrale  sposta\nR G B A  canali,  C  RGB\n[ ] oppure - +  esposizione,  Backspace  reset\nTab  nascondi UI,  F11 / doppio clic  schermo intero\nCtrl+C  copia frame,  Ctrl+S  salva frame,  Ctrl+Shift+R  mostra in Esplora risorse\nCtrl+E  esporta filmato,  Ctrl+B  conversione batch\nAlt+Su / Alt+Giù  versione successiva / precedente\nW  modalità confronto,  X  scambia A/B\nN  controllo NaN,  E  falsi colori,  Z  zebra,  H  scope,  Ctrl+I  metadati") \
    X(AboutText,         "Fast image sequence player with OpenColorIO, ACES 1.3 and ACES 2.0 support.", \
                                                                  "Player veloce di sequenze di immagini con supporto OpenColorIO, ACES 1.3 e ACES 2.0.") \
    X(InOut,             "In/Out",                                "In/Out") \
    X(ClearInOut,        "Clear in/out",                          "Azzera in/out") \
    X(ResetExposure,     "Reset exposure and gamma",              "Reimposta esposizione e gamma") \
    X(Actual,            "actual",                                "effettivi") \
    X(BlenderConfigs,    "Blender (installed)",                   "Blender (installato)") \
    X(AboutTitle,        "About Layerbase Sequence Player",       "Informazioni su Layerbase Sequence Player") \
    X(CreatedBy,         "Created by",                            "Creato da") \
    X(FreewareText,      "Free and open source (MIT license), also for commercial use.", "Gratuito e open source (licenza MIT), anche per uso commerciale.") \
    X(License,           "License",                               "Licenza") \
    X(SourceCode,        "Source code (GitHub)",                  "Codice sorgente (GitHub)") \
    X(ThirdParty,        "Third-party licenses",                  "Licenze di terze parti") \
    X(Info,              "Info",                                  "Info") \
    X(Layer,             "Layer",                                 "Layer") \
    X(MatteOff,          "Off",                                   "Off") \
    X(MatteIds,          "IDs",                                   "ID") \
    X(MatteOverlay,      "Overlay",                               "Evidenzia") \
    X(MatteMasked,       "Masked",                                "Mascherato") \
    X(MatteMatte,        "Matte",                                 "Matte") \
    X(CryptoHint,        "Click objects in the viewer to add or remove them from the mask. Masked and Matte are applied to playback and export.", \
                                                                  "Clicca sugli oggetti nel viewer per aggiungerli o toglierli dalla maschera. Mascherato e Matte valgono per riproduzione ed export.") \
    X(LastPick,          "Last pick",                             "Ultimo click") \
    X(Selected,          "Selected",                              "Selezionati") \
    X(ClearSelection,    "Clear",                                 "Azzera") \
    X(Objects,           "Objects",                               "Oggetti") \
    X(NoManifest,        "No manifest in this file: select by clicking in the viewer.", "Nessun manifest nel file: seleziona cliccando nel viewer.") \
    X(LoadMatte,         "Load Cryptomatte sequence...",          "Carica sequenza Cryptomatte...") \
    X(RemoveMatte,       "Remove",                                "Rimuovi") \
    X(MatteSource,       "Matte from",                            "Matte da") \
    X(MatteOwn,          "this sequence",                         "questa sequenza") \
    X(MatteMissing,      "frames without matte",                  "frame senza matte") \
    X(NoCryptoHere,      "This sequence has no Cryptomatte. Load the Cryptomatte sequence rendered with it to mask it by object or material.", \
                                                                  "Questa sequenza non contiene Cryptomatte. Carica la sequenza Cryptomatte renderizzata insieme per mascherarla per oggetto o materiale.") \
    X(NoCryptoInFile,    "No Cryptomatte layers in",              "Nessun layer Cryptomatte in") \
    X(MatteLoaded,       "Cryptomatte loaded",                    "Cryptomatte caricato") \
    X(Updates,           "Updates",                               "Aggiornamenti") \
    X(CheckUpdates,      "Check for updates (once a day)",        "Controlla aggiornamenti (una volta al giorno)") \
    X(CheckNow,          "Check now",                             "Controlla ora") \
    X(UpdatePrivacy,     "Only a small version file is downloaded from GitHub: no personal data or usage statistics are sent.", \
                                                                  "Viene scaricato solo un piccolo file di versione da GitHub: nessun dato personale o statistica d'uso viene inviato.") \
    X(UpToDate,          "You have the latest version.",          "Hai già l'ultima versione.") \
    X(UpdateFailed,      "Could not check for updates.",          "Impossibile controllare gli aggiornamenti.") \
    X(UpdateAvailable,   "New version available",                 "Nuova versione disponibile") \
    X(InstalledVersion,  "Installed:",                            "Installata:") \
    X(Download,          "Download",                              "Scarica") \
    X(Later,             "Later",                                 "Più tardi") \
    X(SkipVersion,       "Skip this version",                     "Salta questa versione") \
    X(PortableMode,      "Portable mode: settings are saved in",  "Modalità portable: impostazioni salvate in") \
    X(SetIn,             "Set in point (I)",                      "Imposta punto in (I)") \
    X(SetOut,            "Set out point (O)",                     "Imposta punto out (O)") \
    X(ExportAlpha,       "Include alpha (mask / image alpha)",    "Includi alpha (maschera / alpha immagine)") \
    X(BatchMenu,         "Batch convert...",                      "Conversione batch...") \
    X(BatchTitle,        "Batch convert",                         "Conversione batch") \
    X(AddSequences,      "Add sequences...",                      "Aggiungi sequenze...") \
    X(AddFolder,         "Add folder...",                         "Aggiungi cartella...") \
    X(AddCurrent,        "Add current",                           "Aggiungi corrente") \
    X(IncludeSubfolders, "Include subfolders",                    "Includi sottocartelle") \
    X(RemoveUnchecked,   "Remove unchecked",                      "Rimuovi non selezionate") \
    X(ClearList,         "Clear list",                            "Svuota elenco") \
    X(BatchEmpty,        "Add sequences, a folder, or drop files and folders here.", "Aggiungi sequenze, una cartella, o trascina qui file e cartelle.") \
    X(Sequence,          "Sequence",                              "Sequenza") \
    X(Folder,            "Folder",                                "Cartella") \
    X(FramesHeader,      "Frames",                                "Frame") \
    X(Status,            "Status",                                "Stato") \
    X(Destination,       "Destination",                           "Destinazione") \
    X(NextToSequence,    "Next to each sequence",                 "Accanto a ogni sequenza") \
    X(ToFolder,          "Folder",                                "Cartella") \
    X(SkipExisting,      "Skip if the file already exists",       "Salta se il file esiste già") \
    X(InputColor,        "Input color space",                     "Spazio colore input") \
    X(InputAuto,         "Automatic per sequence",                "Automatico per sequenza") \
    X(InputCurrent,      "Current",                               "Attuale") \
    X(UseLayer,          "Use layer when present",                "Usa il layer se presente") \
    X(StartBatch,        "Convert",                               "Converti") \
    X(StatePending,      "waiting",                               "in attesa") \
    X(StateRunning,      "converting",                            "in conversione") \
    X(StateDone,         "done",                                  "completato") \
    X(StateFailed,       "failed",                                "non riuscito") \
    X(StateSkipped,      "skipped (exists)",                      "saltato (esiste)") \
    X(StateCancelled,    "cancelled",                             "annullato") \
    X(BatchFinished,     "Batch finished",                        "Batch completato") \
    X(ExportMovie,       "Export movie...",                       "Esporta filmato...") \
    X(ExportTitle,       "Export movie",                          "Esporta filmato") \
    X(Format,            "Format",                                "Formato") \
    X(Encoder,           "Encoder",                               "Encoder") \
    X(EncoderSoftware,   "Software (best quality)",               "Software (qualità migliore)") \
    X(EncoderNvenc,      "NVIDIA NVENC (fast)",                   "NVIDIA NVENC (veloce)") \
    X(Quality,           "Quality",                               "Qualità") \
    X(QualityHigh,       "High",                                  "Alta") \
    X(QualityMedium,     "Medium",                                "Media") \
    X(QualityLow,        "Low (small file)",                      "Bassa (file piccolo)") \
    X(QualityProRes,     "set by the ProRes profile",             "definita dal profilo ProRes") \
    X(Size,              "Size",                                  "Dimensione") \
    X(Range,             "Range",                                 "Intervallo") \
    X(AllFrames,         "All frames",                            "Tutti i frame") \
    X(ColorBaked,        "Color",                                 "Colore") \
    X(ColorAsViewed,     "baked in as viewed",                    "applicato come visualizzato") \
    X(ColorOff,          "no color management (OCIO off)",        "nessuna gestione colore (OCIO disattivato)") \
    X(OutputFile,        "File",                                  "File") \
    X(Browse,            "Browse...",                             "Sfoglia...") \
    X(FFmpegMissing,     "FFmpeg not found: install it (e.g. winget install ffmpeg) or locate ffmpeg.exe.", \
                                                                  "FFmpeg non trovato: installalo (es. winget install ffmpeg) o indica ffmpeg.exe.") \
    X(LocateFFmpeg,      "Locate ffmpeg.exe...",                  "Indica ffmpeg.exe...") \
    X(Export,            "Export",                                "Esporta") \
    X(Cancel,            "Cancel",                                "Annulla") \
    X(Exporting,         "Exporting",                             "Esportazione") \
    X(Exported,          "Exported",                              "Esportato") \
    X(ExportFailed,      "Export failed",                         "Esportazione non riuscita") \
    X(Remaining,         "remaining",                             "rimanenti") \
    X(ShowInFolder,      "Show in folder",                        "Mostra nella cartella") \
    X(Stack,             "Stack",                                 "Stack") \
    X(StackTitle,        "AOV stack",                             "Stack AOV") \
    X(StackIntro,        "Composite AOVs of this sequence or of other sequences (one per pass): add layers, then set their order and how they blend.", \
                                                                  "Componi gli AOV di questa sequenza o di altre sequenze (una per pass): aggiungi i layer, poi scegli ordine e fusione.") \
    X(StackHint,         "Drag layers to reorder them. Upper layers blend over lower ones in the scene-linear working space; the view is applied once, to the result. Sequences dropped on the window while this panel is open become layers.", \
                                                                  "Trascina i layer per riordinarli. I layer in alto si fondono su quelli sotto nello spazio di lavoro lineare; la vista si applica una volta sola, al risultato. Le sequenze trascinate sulla finestra con questo pannello aperto diventano layer.") \
    X(AddLayer,          "Add layer",                             "Aggiungi layer") \
    X(AddPassSequences,  "Sequences with one pass each...",       "Sequenze con un pass ciascuna...") \
    X(Blend,             "Blend",                                 "Fusione") \
    X(BlendNormal,       "Normal",                                "Normale") \
    X(BlendAdd,          "Add",                                   "Somma") \
    X(BlendSubtract,     "Subtract",                              "Sottrai") \
    X(BlendMultiply,     "Multiply",                              "Moltiplica") \
    X(BlendScreen,       "Screen",                                "Scolora") \
    X(ScreenHint,        "Screen expects values between 0 and 1: with HDR values above 1 the result is not correct.", \
                                                                  "Scolora presuppone valori tra 0 e 1: con valori HDR sopra 1 il risultato non è corretto.") \
    X(Opacity,           "Opacity",                               "Opacità") \
    X(Source,            "Source",                                "Origine") \
    X(Visible,           "Visible",                               "Visibile") \
    X(MoveUp,            "Move up",                               "Sposta su") \
    X(MoveDown,          "Move down",                             "Sposta giù") \
    X(RemoveLayer,       "Remove",                                "Rimuovi") \
    X(CloseStack,        "Close stack",                           "Chiudi stack") \
    X(FramesMissing,     "frames missing",                        "frame mancanti") \
    X(NotASequence,      "Not an image sequence:",                "Non è una sequenza di immagini:") \
    X(StackNoCrypto,     "Not available with the AOV stack",      "Non disponibile con lo stack AOV") \
    X(Resolution,        "Playback resolution: lower uses less memory and reaches the GPU faster. Export always reads full resolution.", \
                                                                  "Risoluzione di riproduzione: più bassa usa meno memoria e arriva prima alla GPU. L'export legge sempre a risoluzione piena.") \
    X(ResFull,           "full",                                  "piena") \
    X(ResHalf,           "half",                                  "metà") \
    X(ResQuarter,        "quarter",                               "un quarto") \
    X(AgxTooltip,        "AgX view transform (Blender formulation), works with any config. SDR: sRGB, Rec.1886 or Display P3 from the selected display.", \
                                                                  "Vista AgX (formulazione Blender), funziona con qualsiasi config. SDR: sRGB, Rec.1886 o Display P3 in base al display scelto.") \
    X(CopyFrame,         "Copy frame to clipboard",               "Copia frame negli appunti") \
    X(SaveFrame,         "Save frame as...",                      "Salva frame con nome...") \
    X(FrameCopied,       "Frame copied to the clipboard",         "Frame copiato negli appunti") \
    X(FrameSaved,        "Frame saved",                           "Frame salvato") \
    X(FrameGrabFailed,   "Cannot capture the frame",              "Impossibile catturare il frame") \
    X(FrameGrabHint,     "Full resolution, with the color, LUT, exposure, channel and mask shown in the viewer.", \
                                                                  "Risoluzione piena, con colore, LUT, esposizione, canale e maschera visibili nel viewer.") \
    X(RevealFrame,       "Show frame in Explorer",                "Mostra frame in Esplora risorse") \
    X(Lut,               "LUT",                                   "LUT") \
    X(LoadLut,           "Load LUT...",                           "Carica LUT...") \
    X(RemoveLut,         "Remove LUT",                            "Rimuovi LUT") \
    X(RecentLuts,        "Recent LUTs",                           "LUT recenti") \
    X(NoLut,             "No LUT loaded. A LUT applies to playback, frame captures, export and batch conversion.", \
                                                                  "Nessuna LUT caricata. La LUT si applica a riproduzione, catture dei frame, export e conversione batch.") \
    X(LutError,          "Cannot read the LUT",                   "Impossibile leggere la LUT") \
    X(LutPosition,       "Apply",                                 "Applica") \
    X(LutDisplay,        "After the view (display)",              "Dopo la vista (display)") \
    X(LutGrading,        "Before the view (grading space)",       "Prima della vista (spazio di grading)") \
    X(LutDisplayHint,    "On the display output: for creative LUTs made for Rec.709 / sRGB.", \
                                                                  "Sull'uscita del display: per LUT creative fatte per Rec.709 / sRGB.") \
    X(LutGradingHint,    "In the config's grading space (color_timing role, ACEScct in ACES configs), before the view transform.", \
                                                                  "Nello spazio di grading della config (ruolo color_timing, ACEScct nelle config ACES), prima della vista.") \
    X(LutNoGrading,      "This config has no color_timing role.", "Questa config non ha il ruolo color_timing.") \
    X(LutRawHint,        "Color management is off: the LUT is applied to the file values.", \
                                                                  "La gestione colore è disattivata: la LUT si applica ai valori del file.") \
    X(Alpha,             "Alpha",                                 "Alpha") \
    X(AlphaStraight,     "Straight",                              "Straight") \
    X(AlphaPremult,      "Premultiplied",                         "Premoltiplicato") \
    X(AlphaHint,         "How the color of this sequence is stored with its alpha. Renders are usually premultiplied (in Cinema 4D, straight only with \"Straight Alpha\" on). Straight images are shown and exported over black.", \
                                                                  "Come è salvato il colore di questa sequenza rispetto all'alpha. I render di solito sono premoltiplicati (in Cinema 4D straight solo con \"Straight Alpha\" attivo). Le immagini straight sono mostrate ed esportate su nero.") \
    X(ExportAlphaKind,   "Alpha color",                           "Colore alpha") \
    X(ExportAlphaHint,   "Straight: what Premiere Pro, Final Cut and most editors expect. Premultiplied: color multiplied by alpha (over black).", \
                                                                  "Straight: quello che si aspettano Premiere Pro, Final Cut e la maggior parte degli editor. Premoltiplicato: colore moltiplicato per l'alpha (su nero).") \
    X(AlphaDetected,     "automatic: premultiplied unless the file declares straight alpha (TIFF)", "automatico: premoltiplicato, salvo file che dichiarano alpha straight (TIFF)") \
    X(ReplaceTitle,      "Open another sequence?",                "Aprire un'altra sequenza?") \
    X(ReplaceText,       "The open sequence will be closed.",     "La sequenza aperta verrà chiusa.") \
    X(ReplaceLoses,      "This work will be lost:",               "Questo lavoro andrà perso:") \
    X(ReplaceStack,      "AOV stack",                             "Stack AOV") \
    X(ReplaceInOut,      "In/Out points",                         "Punti In/Out") \
    X(ReplaceCrypto,     "Cryptomatte selection",                 "Selezione Cryptomatte") \
    X(ReplaceMatte,      "External Cryptomatte sequence",         "Sequenza Cryptomatte esterna") \
    X(ReplaceAlpha,      "Alpha interpretation",                  "Interpretazione alpha") \
    X(DontAskAgain,      "Don't ask again (can be changed in Settings)", "Non chiedere più (modificabile in Impostazioni)") \
    X(ConfirmReplace,    "Ask before replacing the open sequence", "Chiedi prima di sostituire la sequenza aperta") \
    X(Confirmations,     "Confirmations",                         "Conferme") \
    X(OpenAnyway,        "Open",                                  "Apri") \
    X(ReplaceCompare,   "A/B compare", "Confronto A/B") \
    X(LiveRefresh,      "Update the open sequence while it renders", "Aggiorna la sequenza aperta mentre viene renderizzata") \
    X(LiveRefreshHint,  "New and re-rendered frames appear by themselves; frames that could not be read are read again.", "I frame nuovi e quelli renderizzati di nuovo compaiono da soli; i frame illeggibili vengono riletti.") \
    X(FramesAdded,      "new frames", "nuovi frame") \
    X(FramesUpdated,    "frames updated", "frame aggiornati") \
    X(Opening,          "Opening (waiting for the cloud or network drive)", "Apertura (in attesa del disco cloud o di rete)") \
    X(Version,          "Version", "Versione") \
    X(VersionHint,      "Versions of this render next to it (Alt+Up / Alt+Down). Frame, In/Out, color, stack and compare are kept.", "Versioni di questo render accanto a esso (Alt+Su / Alt+Giù). Frame, In/Out, colore, stack e confronto restano.") \
    X(NoNewerVersion,   "No newer version", "Nessuna versione più recente") \
    X(NoOlderVersion,   "No older version", "Nessuna versione precedente") \
    X(NoVersions,       "No other versions found next to this sequence.", "Nessun'altra versione trovata accanto a questa sequenza.") \
    X(CompareTitle,     "Compare A/B", "Confronto A/B") \
    X(CompareWith,      "Compare with sequence...", "Confronta con una sequenza...") \
    X(CompareVersion,   "Compare with version", "Confronta con la versione") \
    X(CompareChange,    "Change B...", "Cambia B...") \
    X(CompareRemove,    "Stop comparing", "Termina confronto") \
    X(CompareWipe,      "Wipe", "Tendina") \
    X(CompareSide,      "Side by side", "Affiancate") \
    X(CompareDiff,      "Difference", "Differenza") \
    X(CompareToggle,    "Toggle A / B", "Alterna A / B") \
    X(CompareSwap,      "Swap A and B", "Scambia A e B") \
    X(CompareGain,      "Gain", "Guadagno") \
    X(CompareHint,      "B frames are matched by frame number. W changes the mode, X swaps A and B; drag the line to move the wipe. Captures and exports use A.", "I frame di B sono abbinati per numero. W cambia modalità, X scambia A e B; trascina la linea per spostare la tendina. Catture ed export usano A.") \
    X(CompareDiffHint,  "Absolute difference of the file values, amplified by the gain: black = identical.", "Differenza assoluta dei valori del file, amplificata dal guadagno: nero = identico.") \
    X(Comparing,        "Comparing with", "Confronto con") \
    X(QcTitle,          "Quality check: pixels, scopes, guides, frame report", "Controllo qualità: pixel, scope, guide, report dei frame") \
    X(Pixels,           "Pixels", "Pixel") \
    X(CheckOff,         "Normal", "Normale") \
    X(CheckBad,         "NaN, Inf and negative values", "Valori NaN, Inf e negativi") \
    X(CheckFalse,       "False color (display exposure)", "Falsi colori (esposizione a display)") \
    X(CheckZebra,       "Zebra (clipped whites, crushed blacks)", "Zebra (bianchi bruciati, neri chiusi)") \
    X(CheckHint,        "Viewer only: never in captures or exports.", "Solo nel viewer: mai nelle catture o negli export.") \
    X(BadNone,          "No NaN, Inf or negative values", "Nessun valore NaN, Inf o negativo") \
    X(BadNeg,           "negative", "negativi") \
    X(Scopes,           "Scopes", "Scope") \
    X(Histogram,        "Histogram", "Istogramma") \
    X(Waveform,         "Waveform", "Forma d'onda") \
    X(Vectorscope,      "Vectorscope", "Vettorscopio") \
    X(ScopesHint,       "Display values of the frame as viewed (color, LUT, exposure, stack).", "Valori di display del frame come visualizzato (colore, LUT, esposizione, stack).") \
    X(Guides,           "Guides", "Guide") \
    X(GuideAspect,      "Aspect mask", "Maschera formato") \
    X(GuideNone,        "None", "Nessuna") \
    X(GuideSafe,        "Safe areas (action 93%, title 90%)", "Aree di sicurezza (azione 93%, titoli 90%)") \
    X(GuideCenter,      "Center cross", "Croce centrale") \
    X(GuideThirds,      "Rule of thirds", "Regola dei terzi") \
    X(FrameReport,      "Frame report...", "Report dei frame...") \
    X(Metadata,         "Metadata", "Metadati") \
    X(MetaHint,         "Header of the current frame's file.", "Intestazione del file del frame corrente.") \
    X(CopyAll,          "Copy all", "Copia tutto") \
    X(CopiedText,       "Copied to the clipboard", "Copiato negli appunti") \
    X(ReportTitle,      "Frame report", "Report dei frame") \
    X(ReportMissing,    "Missing frames", "Frame mancanti") \
    X(ReportNoMissing,  "No missing frames", "Nessun frame mancante") \
    X(ReportSmall,      "Suspicious files (empty or much smaller than the others)", "File sospetti (vuoti o molto più piccoli degli altri)") \
    X(ReportErrors,     "Frames that cannot be read", "Frame illeggibili") \
    X(ReportCheckAll,   "Read every frame", "Leggi tutti i frame") \
    X(ReportChecking,   "Reading frames", "Lettura dei frame") \
    X(ReportCheckHint,  "Decodes every frame in the background to find damaged files. Otherwise only the frames already decoded are known.", "Decodifica tutti i frame in background per trovare i file danneggiati. Altrimenti si conoscono solo i frame già decodificati.") \
    X(ReportAllOk,      "Every frame can be read", "Tutti i frame sono leggibili") \
    X(ReportNoErrors,   "No errors in the decoded frames", "Nessun errore nei frame decodificati") \
    X(CopyReport,       "Copy report", "Copia report") \
    X(Expected,         "expected", "attesi") \
    X(EmptyFile,        "empty file", "file vuoto") \
    X(SmallFile,        "much smaller than usual", "molto più piccolo del solito") \
    X(GapBefore,        "missing before this frame", "mancanti prima di questo frame") \
    X(Framing,          "Framing", "Inquadratura") \
    X(FullFrame,        "Full frame", "Fotogramma intero") \
    X(AspectCrop,       "Crop", "Ritaglia") \
    X(AspectBars,       "Black bars", "Bande nere") \
    X(BurnIn,           "Burn-in", "Sovrimpressione") \
    X(BurnInOn,         "Burn text into the frames", "Imprimi testo nei frame") \
    X(BurnName,         "Shot", "Shot") \
    X(BurnFrame,        "Frame", "Frame") \
    X(BurnTimecode,     "Timecode", "Timecode") \
    X(BurnDate,         "Date", "Data") \
    X(BurnText,         "Custom text", "Testo libero") \
    X(Filters,          "Filters", "Filtri") \
    X(Denoise,          "Denoise", "Riduzione rumore") \
    X(DenoiseHint,      "Removes the render noise of the viewed sequence. Each frame is denoised once, then plays from the cache; exports use the denoised frames.", "Rimuove il rumore di render della sequenza visualizzata. Ogni frame viene ripulito una volta e poi riprodotto dalla cache; gli export usano i frame ripuliti.") \
    X(DenoiseEngine,    "Engine", "Motore") \
    X(EngineOidn,       "Open Image Denoise (Intel) \xC2\xB7 CPU or GPU", "Open Image Denoise (Intel) \xC2\xB7 CPU o GPU") \
    X(EngineOptix,      "OptiX (NVIDIA) \xC2\xB7 GPU", "OptiX (NVIDIA) \xC2\xB7 GPU") \
    X(DenoiseDevice,    "Device", "Dispositivo") \
    X(DeviceAuto,       "Automatic (fastest)", "Automatico (il più veloce)") \
    X(DeviceCpu,        "CPU", "CPU") \
    X(DeviceGpu,        "GPU", "GPU") \
    X(DnQualityHigh,    "High (final frames)", "Alta (frame finali)") \
    X(DnQualityBalanced, "Balanced", "Bilanciata") \
    X(DnQualityFast,    "Fast (preview)", "Veloce (anteprima)") \
    X(DenoiseGuides,    "Guides (EXR layers)", "Guide (layer EXR)") \
    X(GuideAlbedo,      "Albedo", "Albedo") \
    X(GuideNormal,      "Normal", "Normali") \
    X(GuideAuto,        "Automatic", "Automatico") \
    X(GuideOff,         "None", "Nessuna") \
    X(GuidesHint,       "Albedo and normal passes of the same EXR (e.g. Denoising Albedo / Denoising Normal) keep textures and edges sharp. A normal guide needs an albedo guide.", "I pass albedo e normali dello stesso EXR (es. Denoising Albedo / Denoising Normal) mantengono nitidi texture e bordi. La guida normali richiede quella albedo.") \
    X(GuidesNoLayers,   "Only multilayer EXR files have guide layers.", "Solo gli EXR multilayer hanno layer guida.") \
    X(OidnNotInstalled, "Open Image Denoise is not installed yet: the first use needs a one-time download.", "Open Image Denoise non è ancora installato: il primo utilizzo richiede un download (una sola volta).") \
    X(OidnDownloadInfo, "Downloads Intel Open Image Denoise %s (%d MB) from github.com/RenderKit/oidn, checks it and keeps the libraries (%d MB) in:", "Scarica Intel Open Image Denoise %s (%d MB) da github.com/RenderKit/oidn, lo verifica e salva le librerie (%d MB) in:") \
    X(OidnDownloadInfo2, "They are loaded only while the denoiser is in use: opening the program stays as fast as before. License: Apache 2.0.", "Vengono caricate solo mentre il denoiser è in uso: l'apertura del programma resta veloce come prima. Licenza: Apache 2.0.") \
    X(DownloadRequired, "Download required", "Download necessario") \
    X(DownloadAndEnable, "Download and enable", "Scarica e attiva") \
    X(DownloadOidn,     "Download Open Image Denoise...", "Scarica Open Image Denoise...") \
    X(Downloading,      "Downloading", "Download in corso") \
    X(Verifying,        "Verifying the download...", "Verifica del download...") \
    X(Extracting,       "Extracting...", "Estrazione...") \
    X(OidnInstalled,    "Open Image Denoise installed", "Open Image Denoise installato") \
    X(DownloadFailed,   "Download failed", "Download non riuscito") \
    X(Retry,            "Retry", "Riprova") \
    X(InstalledIn,      "Installed in", "Installato in") \
    X(RemoveLibraries,  "Remove", "Rimuovi") \
    X(RemoveLibrariesHint, "Deletes the downloaded libraries. They are downloaded again on the next use.", "Elimina le librerie scaricate. Verranno scaricate di nuovo al prossimo utilizzo.") \
    X(OptixInfo,        "Part of the NVIDIA driver: nothing to download. Needs an NVIDIA GPU with driver R535 or later.", "Fa parte del driver NVIDIA: niente da scaricare. Richiede una GPU NVIDIA con driver R535 o successivo.") \
    X(OptixNoDriver,    "No NVIDIA driver found on this computer.", "Nessun driver NVIDIA trovato su questo computer.") \
    X(OptixNotBuilt,    "This build does not include OptiX.", "Questa build non include OptiX.") \
    X(MsPerFrame,       "ms per frame", "ms per frame") \
    X(DenoiseBusy,      "denoising...", "denoise in corso...") \
    X(DenoiseNoStack,   "Not applied to the AOV stack: it works on the single view.", "Non si applica allo stack AOV: lavora sulla vista singola.") \
    X(DenoiseFlicker,   "Frames are denoised one at a time: slight flicker between frames is possible; guides reduce it.", "I frame vengono ripuliti uno alla volta: è possibile un leggero sfarfallio tra i frame; le guide lo riducono.") \
    X(DenoiseStackHint, "In the AOV stack the denoise is turned on per layer (Stack panel); the engine and the guides set here apply to every layer.", "Nello stack AOV il denoise si attiva per singolo layer (pannello Stack); motore e guide impostati qui valgono per tutti i layer.") \
    X(DenoiseLayerHint, "Engine and guides: Filters panel", "Motore e guide: pannello Filtri") \
    X(OptixTemporal,    "Temporal (no flicker between frames)", "Temporale (niente sfarfallio tra i frame)") \
    X(OptixTemporalHint, "Frames are denoised in order, each one together with the previous denoised frame. A jump in the timeline starts a new chain.", "I frame vengono ripuliti in ordine, ognuno insieme al frame precedente già ripulito. Un salto nella timeline fa ripartire la catena.") \
    X(GuideMotion,      "Motion", "Movimento") \
    X(FlowInvert,       "Invert", "Inverti") \
    X(FlowFlipY,        "Flip Y", "Inverti Y") \
    X(GuidesTemporalHint, "The temporal model uses no normal guide.", "Il modello temporale non usa la guida normali.") \
    X(MotionAov,        "Motion vector AOV", "AOV motion vector") \
    X(MotionEstimate,   "Estimated from the frames", "Stimato dai frame") \
    X(MotionAovHint,    "Lets the previous frame follow moving objects (e.g. the Cycles Vector pass, in this EXR or in a sequence of its own). Without it the motion is estimated by the Optical Flow unit of the GPU (GeForce RTX 20 / GTX 16 or later). If objects trail more with the AOV, try Invert or Flip Y.", "Fa seguire al frame precedente gli oggetti in movimento (es. il pass Vector di Cycles, in questo EXR o in una sequenza a parte). Senza, il movimento viene stimato dall'unità Optical Flow della GPU (GeForce RTX 20 / GTX 16 o successive). Se con l'AOV le scie aumentano, prova Inverti o Inverti Y.") \
    X(LoadMotionAov,    "Load motion vector AOV sequence...", "Carica sequenza AOV motion vector...") \
    X(MotionAovLoaded,  "Motion vector AOV loaded", "AOV motion vector caricata") \
    X(RemoveMotionAov,  "Remove", "Rimuovi") \
    X(MotionAovMissing, "frames without motion vectors", "frame senza motion vector") \
    X(NotMotionAov,     "Not a motion vector AOV (EXR with 2 or more channels):", "Non è una AOV motion vector (EXR con almeno 2 canali):") \
    X(AntiGhost,        "Reduce trails", "Riduci le scie") \
    X(AntiGhostHint,    "Where the previous frame does not match this one (uncovered areas, missing or wrong motion) the result stays within the colors of this frame: no trails.", "Dove il frame precedente non corrisponde a questo (zone scoperte, movimento assente o sbagliato) il risultato resta entro i colori di questo frame: niente scie.") \
    X(MotionByAov,      "motion vector AOV", "AOV motion vector") \
    X(MotionByEstimate, "estimated (NVIDIA Optical Flow)", "stimato (NVIDIA Optical Flow)") \
    X(MotionByNone,     "none: image assumed still", "nessuno: immagine considerata ferma") \
    X(DenoiseStrength,  "Strength", "Intensità") \
    X(DenoiseStrengthHint, "Mix with the original frame: below 100% some grain and fine detail come back.", "Miscela con il frame originale: sotto il 100% tornano un po' di grana e di dettaglio fine.") \
    X(ReplaceMotionAov, "Motion vector AOV sequence", "Sequenza AOV motion vector") \
    X(BatchDenoise,     "Denoise every sequence", "Ripulisci ogni sequenza") \
    X(BatchDenoiseHint, "With the engine and guides of the Filters panel.", "Con motore e guide del pannello Filtri.")

enum class S {
#define X(id, en, it) id,
    STRING_TABLE(X)
#undef X
    Count
};

void SetLanguage(Lang lang);
Lang GetLanguage();
const char* tr(S id);
