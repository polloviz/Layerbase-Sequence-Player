#pragma once
#include "BurnIn.h"
#include "ColorManager.h"
#include "DirWatcher.h"
#include "Export.h"
#include "FrameCache.h"
#include "GLViewer.h"
#include "Metadata.h"
#include "Sequence.h"
#include "Settings.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

typedef struct HWND__* HWND;
typedef struct HDC__* HDC;
typedef struct HGLRC__* HGLRC;
struct ImFont;

struct StartupOptions {
    std::string config, display, view;   // empty = from settings
    std::wstring exportPath;              // non-empty: export then quit
    std::string exportCodec;              // h264 | h265 | prores-proxy | prores-lt | prores | prores-hq | prores-4444
    bool exportHardware = false;
    bool exportAlpha = false;
    bool exportPremultiplied = false;     // with alpha: premultiplied instead of straight color
    std::string layer, matte, cryptoLayer, cryptoSelect;   // EXR layer / Cryptomatte setup
    std::wstring matteSeq;                                  // external Cryptomatte sequence
    std::wstring batchRoot, batchOutDir;                    // --batch: convert all sequences, then quit
    bool overwrite = false;
    int proxy = 1;                        // playback resolution divisor (2 = half, 4 = quarter)
    std::string aspect;                   // --aspect 2.39:1 (export framing), --bars
    bool aspectBars = false;
    std::string burnIn, burnText;         // --burn-in name,frame,timecode,date  --burn-text "..."
    std::string denoise;                  // --denoise oidn|oidn-cpu|oidn-gpu|optix|optix-temporal
};

enum class LoopMode : int { Loop = 0, Once = 1, PingPong = 2 };

// Alpha kind of an image when the user has not chosen one: straight only when the file says so.
AlphaMode AutoAlphaMode(const ImagePtr& img);

// A layer of the AOV stack: an EXR layer (or the whole image) of the opened sequence or
// of another sequence holding one pass, matched frame by frame.
struct StackLayer {
    int id = 0;                                              // stable ImGui id
    std::string name;
    std::string key;                                         // what is decoded (LayerLoad::key)
    std::shared_ptr<const Sequence> seq;                     // other sequence; null = the opened one
    std::shared_ptr<const std::vector<std::wstring>> files;  // its file per frame of the opened one
    int missing = 0;                                         // frames of the opened sequence without a file
    std::shared_ptr<const std::vector<ExrLayer>> exrLayers;  // layers of the source (null = not EXR)
    int exrLayer = -1;
    bool isFloat = true;                                     // EXR / HDR source
    std::string input;                                       // OCIO input color space
    bool inputAuto = true;
    BlendMode blend = BlendMode::Normal;
    float opacity = 1.0f;
    float exposure = 0.0f;                                   // stops
    bool visible = true;
    bool denoise = false;                                    // with the engine set in the Filters panel
    std::string loadKey;                                     // key of the decoded (filtered) pixels; "" = key
};

// Denoiser guide passes, picked among the EXR layers of the denoised file. Flow = the motion
// vector AOV, which can also come from a sequence of its own.
enum class GuideKind { Albedo, Normal, Flow };

class App {
public:
    static constexpr unsigned kDirChangedMsg = 0x8000 + 2;   // WM_APP + 2: the watched folder changed

    int run(const std::wstring& initialPath, double fpsOverride, bool autoplay, const StartupOptions& opts);

    // Called from the window procedure.
    long long handleMessage(HWND hwnd, unsigned msg, unsigned long long wParam, long long lParam, bool& handled);

private:
    // lifecycle
    bool createWindow();
    bool createGLContext();
    void initImGui();
    void applyStyle(float dpiScale);
    void shutdown();
    void frame();

    // actions
    // Finding the frames and reading the first header run on a worker. When that takes more
    // than a moment (a cloud drive downloading an online-only file, a slow share) the window
    // stays responsive and the sequence replaces the shown one once ready; `then` runs after
    // it opened. wait = finish before returning (batch, command-line export).
    void openPath(const std::wstring& path, bool addToRecent = true, std::function<void()> then = {}, bool wait = false);
    void pollOpen();
    void requestOpen(const std::wstring& path);   // asks first when it would replace the open sequence
    std::vector<const char*> workToLose() const;   // what opening another sequence resets
    void openDialog(bool folder);
    void loadConfig(const std::string& source, bool persist = true);
    void loadCustomConfigDialog();
    void loadLutDialog();
    void setLut(const std::string& path);   // "" = none
    void chooseInputForSequence();
    void setInput(const std::string& name, bool userChoice);
    void setPlaying(int direction);
    void step(int delta);
    void seek(int index);
    void toggleFullscreen();
    void applyLanguage();
    void applyCacheBudget();
    void showToast(const std::string& text, bool error = false);
    void updateTitle();
    void claimSequence();
    void activateOtherInstance();
    void defer(std::function<void()> fn) { m_deferred.push_back(std::move(fn)); }
    void runDeferred();

    // per frame
    void updatePlayback();
    void handleViewerInput(float vx, float vy, float vw, float vh);
    void handleShortcuts();
    void rebuildColorIfNeeded();

    // layers / Cryptomatte (CryptoUI.cpp)
    void applyLoadOptions();
    void applyStartupLayers(const StartupOptions& opts);
    void setLayer(int index);
    void setMatteMode(MatteMode m);
    void toggleCryptoId(uint32_t id);
    void pickCrypto();
    void drawCryptoPanel(float x, float y, float w, float h);
    void drawLayerCombo(float width);
    void loadMatteDialog();
    void loadMatteSequence(const std::wstring& path);
    void clearMatteSequence();
    bool hasLayers() const { return m_exrInfo.layers.size() > 1; }
    // Cryptomatte layers in use: the external matte sequence when loaded, else the frame's own.
    const std::vector<CryptoLayer>& cryptoLayers() const { return m_matteSeq ? m_matteInfo.cryptos : m_exrInfo.cryptos; }
    bool hasCrypto() const { return !cryptoLayers().empty(); }
    std::string currentLayerLabel() const;

    // AOV stack (StackUI.cpp)
    bool stackActive() const { return !m_stack.empty(); }
    void applyLoadPlan();                                    // what the cache decodes: the view or the stack layers
    void showFrame(const FrameSetPtr& set);
    ImagePtr baseImage(const FrameSetPtr& set) const;        // sets the canvas: the lowest stack layer with pixels
    std::vector<CompLayer> compLayers(const FrameSetPtr& set) const;
    std::string autoInput(bool isFloat, const std::wstring& path) const;
    void stackBegin();
    void stackAddOwn(int exrLayer);
    void stackAddOther(const StackLayer& source, int exrLayer);
    void stackAddSequences(const std::vector<std::wstring>& paths);
    void stackPush(StackLayer layer);
    void stackRemove(int index);
    void stackMove(int from, int to);
    void stackClear();
    void stackSetExrLayer(int index, int exrLayer);
    void stackSetInput(int index, const std::string& name);  // "" = automatic
    void stackRefreshInputs();                               // after a config change
    void stackAddDialog();
    void stackTestSetup();
    void drawStackPanel(float x, float y, float w, float h);
    void drawStackAddMenu();
    bool makeSequenceLayer(const std::wstring& path, StackLayer& out);   // a layer from another sequence

    // The sequence on disk (SequenceUI.cpp): live refresh, versions, frame report
    void startWatching();
    void refreshSequence();
    void switchVersion(int delta);                           // +1 newer, -1 older
    void openVersion(const SequenceVersion& v);              // keeps frame, range, color, stack, compare
    void drawVersionCombo();
    void drawFrameReport();
    void startFrameCheck();
    std::string frameReportText() const;

    // A/B compare (CompareUI.cpp): B frames matched by number, decoded with A
    bool compareActive() const { return m_cmpSeq != nullptr; }
    void setCompare(const std::wstring& path, bool announce = true);
    void clearCompare();
    void rematchCompare();                                   // after A or B changed on disk
    void resetCompare();                                     // forget B (no plan update)
    void drawCompareMenu();
    ImagePtr compareImage(const FrameSetPtr& set) const;
    std::string compareInput() const;

    // QC (QcUI.cpp): pixel checks, guides, scopes
    void setCheck(CheckMode m);
    void drawQcMenu();
    void drawGuides(float vx, float vy, float vw, float vh);
    void drawQcOverlays(float vx, float vy, float vw, float vh);
    void updateScopes();
    void drawScopes();
    void countBadPixels();

    // Metadata panel (InfoUI.cpp)
    void drawInfoPanel(float x, float y, float w, float h);

    // Filters (FilterUI.cpp): denoise of the viewed sequence or of single stack layers, exports
    // and batch conversion included
    bool denoiseActive() const { return m_denoise && !stackActive(); }
    bool denoiseInUse() const;                     // the view or a stack layer is denoised
    DenoiseSpecPtr denoiseSpec() const;            // the view's filter for the decode plan; null = off
    DenoiseSpecPtr makeDenoiseSpec(const std::vector<ExrLayer>& layers) const;   // guides among `layers`
    void setDenoise(bool on);                      // asks to download Open Image Denoise first when needed
    void stackSetDenoise(int index, bool on);
    // True when the engine can run now; otherwise the download dialog opens and `then` runs once installed.
    bool requireDenoiser(std::function<void()> then);
    void denoiseSettingsChanged();
    void denoiseUnused();                          // frees the devices when nothing is denoised any more
    // Layer index in guideLayers(kind, layers); -1 = none.
    int denoiseGuide(GuideKind kind, const std::vector<ExrLayer>& layers) const;
    // Where a guide is picked from: `layers` (the denoised file), or the motion vector AOV sequence.
    const std::vector<ExrLayer>& guideLayers(GuideKind kind, const std::vector<ExrLayer>& layers) const;
    void loadMotionDialog();
    bool loadMotionSequence(const std::wstring& path);
    void clearMotionSequence();
    void rematchMotionSequence();                  // after the opened sequence changed on disk
    void drawFilterPanel(float x, float y, float w, float h);
    void drawGuideCombo(const char* id, GuideKind kind, float width);
    void drawDenoiseDownload();                    // modal: what is downloaded, where, progress
    ImagePtr stackImage(const FrameSetPtr& set, const StackLayer& s) const;   // filtered, else as decoded

    // Where the image is on screen (screen pixels, y down), as GLViewer::draw places it.
    // half: -1 whole viewport, 0 / 1 left / right half (side-by-side compare).
    struct ScreenRect { float x0 = 0, y0 = 0, x1 = 0, y1 = 0; };
    ScreenRect imageRect(float vx, float vy, float vw, float vh, int half = -1) const;
    bool sideBySide() const { return compareActive() && m_cmpMode == CompareMode::SideBySide; }

    // Current frame (FrameUI.cpp)
    AlphaMode alphaModeFor(const ImagePtr& img) const;   // for an image of the opened sequence
    // The current frame at full resolution through the viewer pipeline, as packed RGB.
    bool grabFrame(std::vector<uint8_t>& out, int& width, int& height, bool sixteenBit);
    void copyFrame();
    void saveFrameDialog();
    void revealFrame();

    // UI (UI.cpp)
    void drawUI();
    void drawReplaceConfirm();
    void drawLutButton(float width);
    void drawAlphaCombo(float width);
    void drawTopBar();
    void drawBottomBar();
    void drawTimeline(float width, float height);
    void drawTransport();
    void drawEmptyState(float vx, float vy, float vw, float vh);
    void drawOverlays(float vx, float vy, float vw, float vh);
    void drawMainMenu();
    void drawSettings();
    void drawAbout();
    void drawConfigCombo(float width);
    // Returns true when the user picks a space (`picked`, "" = automatic).
    bool drawColorSpaceCombo(const char* id, float width, const std::string& current, bool isAuto, std::string& picked);
    void drawDisplayViewCombos(float wDisplay, float wView, float wLook);

    // Batch conversion (BatchUI.cpp)
    void openBatchDialog();
    void batchAddPaths(const std::vector<std::wstring>& paths);
    void drawBatchDialog();
    void processBatch();
    void startBatch();
    void endBatchDenoise();                        // the view's denoise as it was before the batch
    void cancelBatch();

    // Update check (UpdateUI.cpp)
    void startUpdateCheck(bool manual);
    void pollUpdateCheck();
    void drawUpdateBanner();
    void drawUpdateSettings();

    // Movie export (ExportUI.cpp)
    void openExportDialog();
    void drawExportDialog();
    void drawCodecRows(ExportOptions& o, float labelW);   // format / encoder / quality / alpha / size
    void drawExportProgress();
    void startExport();
    void processExport();
    void deriveColorTags(ExportOptions& opt) const;
    BurnInText burnInText(int index) const;               // texts burned into frame `index`
    void frameExport(std::vector<uint8_t>& px, int width, int height, int index);   // bars and burn-in
    std::wstring defaultExportPath() const;

    int frameCount() const { return m_seq ? m_seq->count() : 0; }
    int nextIndex(int from, int dir, bool& reverse) const;

    // window / GL
    HWND m_hwnd = nullptr;
    HDC m_hdc = nullptr;
    HGLRC m_glrc = nullptr;
    bool m_ready = false;
    bool m_inFrame = false;
    std::vector<std::function<void()>> m_deferred;   // run outside ImGui frame (dialogs)
    bool m_fullscreen = false;
    long m_savedStyle = 0;
    struct { long left, top, right, bottom; } m_savedRect{};
    bool m_savedMaximized = false;
    float m_dpiScale = 1.0f;
    int m_renderFrames = 3;       // frames to render before idling
    int m_framesRendered = 0;
    ImFont* m_fontUI = nullptr;
    ImFont* m_fontMono = nullptr;

    // subsystems
    Settings m_settings;
    ColorManager m_color;
    GLViewer m_viewer;
    FrameCache m_cache;

    // sequence / playback
    std::shared_ptr<Sequence> m_seq;
    void* m_seqMutex = nullptr;       // named per sequence: dedupes Explorer multi-select launches
    bool m_seqOpenElsewhere = false;  // another instance already shows this sequence
    std::wstring m_seqExt;
    int m_index = 0;
    int m_in = 0, m_out = 0;
    int m_playDir = 0;            // -1, 0, +1
    double m_fps = 30.0;
    LoopMode m_loop = LoopMode::Loop;
    double m_nextTick = 0.0;
    double m_lastFrameTime = 0.0;
    double m_actualFps = 0.0;
    ImagePtr m_shown;                 // the image, or the canvas-setting layer of the stack
    FrameSetPtr m_shownSet;
    std::vector<uint8_t> m_cacheMask;
    double m_lastMaskUpdate = 0.0;
    double m_viewerMsMax = 0.0;       // slowest viewer update + draw since the last playback log

    // view
    bool m_fit = true;
    float m_zoom = 1.0f;
    float m_panX = 0.0f, m_panY = 0.0f;
    ChannelMode m_channel = ChannelMode::RGB;
    bool m_uiVisible = true;
    bool m_hoverValid = false;
    int m_hoverX = 0, m_hoverY = 0;
    float m_hoverRGBA[4] = {};
    float m_topBarH = 0, m_bottomBarH = 0;

    // color
    bool m_colorManaged = true;
    OCIO::ConstGPUProcessorRcPtr m_prebuiltGpu;   // built during startup
    bool m_colorDirty = true;
    bool m_inputUserChosen = false;
    float m_exposure = 0.0f;
    float m_gamma = 1.0f;
    std::string m_colorError;
    int m_alphaOverride = -1;             // AlphaMode chosen for the opened sequence; -1 = from the file type

    // EXR layers / Cryptomatte
    ExrInfo m_exrInfo;
    int m_layer = 0;                      // index into m_exrInfo.layers
    int m_crypto = 0;                     // index into m_exrInfo.cryptos
    MatteMode m_matte = MatteMode::Off;
    std::vector<uint32_t> m_cryptoSel;    // sorted IDs
    bool m_cryptoPanel = false;
    char m_cryptoFilter[128] = {};
    std::string m_lastPick;
    float m_panelW = 0;                   // right side panel width (px)
    LoadOptionsPtr m_loadOpts;
    // External Cryptomatte sequence masking the viewed one (matched by frame number)
    std::shared_ptr<Sequence> m_matteSeq;
    ExrInfo m_matteInfo;
    std::shared_ptr<const std::vector<std::wstring>> m_matteFiles;   // per frame of m_seq, "" = missing
    int m_matteMissing = 0;
    int m_viewSerial = 0;                 // key of the single-view decode plan
    int m_proxy = 1;                      // playback resolution chosen by the user: 1, 2 (half) or 4 (quarter)
    int m_planProxy = 1;                  // resolution being decoded: full while exporting

    // AOV stack: layers bottom to top, blended in the working space; empty = single view
    std::vector<StackLayer> m_stack;
    int m_stackSel = 0;
    int m_stackNextId = 1;
    bool m_stackPanel = false;
    std::vector<std::string> m_stackInputs;   // input spaces with a GPU transform (CompLayer::transform)
    std::string m_hoverLayer;                 // pixel inspector source in the stack

    // Live refresh: the folder of the open sequence is watched once the first frame is up
    DirWatcher m_watcher;
    double m_refreshFirst = 0.0, m_refreshDue = 0.0;   // pending change: first notice, quiet deadline
    std::vector<SequenceVersion> m_versions;           // listed when the version menu opens

    // A/B compare
    std::shared_ptr<const Sequence> m_cmpSeq;
    std::shared_ptr<const std::vector<std::wstring>> m_cmpFiles;   // per frame of m_seq, "" = missing
    int m_cmpMissing = 0;
    LoadOptionsPtr m_cmpOpts;
    std::string m_cmpKey;
    bool m_cmpIsFloat = true;
    std::string m_cmpInput;                   // B input space; "" = the same as A
    CompareMode m_cmpMode = CompareMode::Wipe;
    float m_wipe = 0.5f;
    bool m_cmpSwap = false;
    float m_diffGain = 10.0f;
    bool m_wipeDrag = false;
    bool m_hoverB = false;                    // the pixel inspector reads B

    // QC
    CheckMode m_check = CheckMode::Off;
    ImagePtr m_badImage;                      // image the counts below belong to
    size_t m_badNan = 0, m_badNeg = 0;
    bool m_scopesOpen = false;
    double m_scopesAt = 0.0;
    std::vector<float> m_hist;                // 4 x 256 bins: R, G, B, luma
    std::vector<uint8_t> m_wave, m_vector;    // 256 x 256 RGBA scope images
    unsigned m_waveTex = 0, m_vectorTex = 0;
    bool m_scopesValid = false;

    // Filters. The denoise is not saved: every session starts without it (nothing loaded at startup).
    bool m_filterPanel = false;
    bool m_denoise = false;
    // EXR layer labels; "" = automatic, "-" = none. Motion: "~" = estimated from the frames.
    std::string m_guideAlbedo, m_guideNormal, m_guideFlow;
    // Motion vector AOV from a sequence of its own, matched by frame number; null = the AOV is a
    // layer of the denoised file. It belongs to the opened shot.
    std::shared_ptr<const Sequence> m_mvSeq;
    std::shared_ptr<const std::vector<std::wstring>> m_mvFiles;   // per frame of the opened sequence
    std::vector<ExrLayer> m_mvLayers;
    int m_mvMissing = 0;
    bool m_openDenoiseDownload = false;
    std::shared_ptr<OidnInstall> m_oidnInstall; // download running or finished (until the dialog closes)
    std::function<void()> m_afterOidnInstall;   // what asked for the download (filter on, batch start)
    bool m_batchDenoiseSaved = false;           // the view's denoise state while a batch overrides it
    std::string m_oidnRemoveError;

    // Metadata panel
    bool m_infoPanel = false;
    std::vector<MetaEntry> m_meta;
    std::wstring m_metaPath;
    uint64_t m_metaStamp = 0;
    double m_metaAt = 0.0;
    char m_metaFilter[128] = {};

    // Frame report
    bool m_reportOpen = false;
    struct FrameCheck;                        // background decode of every frame
    std::shared_ptr<FrameCheck> m_frameCheck;

    // batch
    struct BatchItem {
        std::wstring path;                 // first frame
        std::wstring name, dir, base;
        int frames = 0, first = 0, last = 0;
        enum class State { Pending, Running, Done, Failed, Skipped, Cancelled } state = State::Pending;
        std::string message;
        std::wstring output;
        bool include = true;
    };
    std::vector<BatchItem> m_batch;
    bool m_openBatch = false, m_batchDialogOpen = false;
    bool m_batchRunning = false, m_batchWaitingFrame = false, m_batchAwaitingResult = false;
    int m_batchCurrent = -1;
    ExportOptions m_batchOpt;
    std::string m_batchLayer;              // layer label to use when present ("" = default)
    std::string m_batchInput;              // input color space ("" = auto per sequence)
    std::vector<std::wstring> m_batchOutputs;
    double m_batchStartTime = 0.0;
    bool m_quitAfterBatch = false;

    // last export result (read by the batch runner)
    bool m_exportResultReady = false, m_exportResultOk = false;
    std::string m_exportResultMsg;

    // export
    bool m_openExport = false;
    ExportOptions m_exportOpt;
    FFmpegInfo m_ffmpeg;
    bool m_exportInOut = false;
    std::unique_ptr<MovieExporter> m_exporter;
    int m_exportFirst = 0, m_exportLast = 0, m_exportNext = 0;
    bool m_exportFinishing = false;
    double m_exportStartTime = 0.0;
    std::wstring m_lastExport;
    StartupOptions m_cliExport;           // pending command-line export
    bool m_quitAfterExport = false;
    bool m_batchMode = false;             // --export: never touch saved settings
    bool m_persistColor = true;           // false while a --config/--display/--view override is active
    int m_exitCode = 0;

    // update check: the worker fills the shared state, the UI thread polls it
    struct UpdateCheckState;
    std::shared_ptr<UpdateCheckState> m_updateCheck;
    std::string m_updateVersion, m_updateUrl, m_updateNotes;   // non-empty version = banner shown

    // ui state
    std::wstring m_pendingOpen;           // waits for the replace confirmation
    struct OpenState;
    std::shared_ptr<OpenState> m_opening;   // a slow open still running on its worker
    bool m_openReplace = false, m_replaceDontAsk = false;
    bool m_openSettings = false;
    bool m_openAbout = false;
    int m_aboutPage = 0;              // 0 info, 1 license, 2 third-party
    unsigned m_logoTex = 0;
    std::string m_licenseText, m_noticesText;
    char m_filter[128] = {};
    std::string m_toast;
    bool m_toastError = false;
    double m_toastUntil = 0.0;
    bool m_scrubbing = false;
    int m_scrubResumeDir = 0;
};
