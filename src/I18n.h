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
    X(ShortcutsText,     "Space  play/pause\nJ / K / L  reverse / stop / forward\nLeft / Right  step\nHome / End  first / last\nI / O  in / out point,  U  clear\nF  fit,  1  100%,  wheel  zoom,  middle drag  pan\nR G B A  channels,  C  RGB\n[ ] or - +  exposure,  Backspace  reset\nTab  hide UI,  F11 / double click  fullscreen\nCtrl+E  export movie,  Ctrl+B  batch convert", \
                                                                  "Spazio  play/pausa\nJ / K / L  indietro / stop / avanti\nSinistra / Destra  frame singolo\nHome / Fine  primo / ultimo\nI / O  punto in / out,  U  azzera\nF  adatta,  1  100%,  rotella  zoom,  tasto centrale  sposta\nR G B A  canali,  C  RGB\n[ ] oppure - +  esposizione,  Backspace  reset\nTab  nascondi UI,  F11 / doppio clic  schermo intero\nCtrl+E  esporta filmato,  Ctrl+B  conversione batch") \
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
                                                                  "Vista AgX (formulazione Blender), funziona con qualsiasi config. SDR: sRGB, Rec.1886 o Display P3 in base al display scelto.")

enum class S {
#define X(id, en, it) id,
    STRING_TABLE(X)
#undef X
    Count
};

void SetLanguage(Lang lang);
Lang GetLanguage();
const char* tr(S id);
