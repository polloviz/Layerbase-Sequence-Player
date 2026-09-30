#pragma once
#include "ColorManager.h"
#include "Export.h"
#include "FrameCache.h"
#include "GLViewer.h"
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
};

class App {
public:
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
    void openPath(const std::wstring& path, bool addToRecent = true);
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
