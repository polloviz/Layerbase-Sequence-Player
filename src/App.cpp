#include <GL/glew.h>
#include "App.h"
#include "I18n.h"
#include "ImageIO.h"
#include "Platform.h"
#include "Version.h"

#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_win32.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <ctime>
#include <future>
#include <mutex>
#include <thread>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static constexpr UINT WM_APP_FRAME_READY = WM_APP + 1;
static App* g_app = nullptr;

static double Now()
{
    static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return double(t.QuadPart) / double(freq.QuadPart);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) return 1;
    if (g_app) {
        bool handled = false;
        LRESULT r = (LRESULT)g_app->handleMessage(hwnd, msg, wParam, lParam, handled);
        if (handled) return r;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Lifecycle

int App::run(const std::wstring& initialPath, double fpsOverride, bool autoplay, const StartupOptions& opts)
{
    g_app = this;
    Log("start");
    m_settings.load();
    applyLanguage();
    m_fps = m_settings.defaultFps;
    m_loop = (LoopMode)m_settings.loopMode;
    m_color.lutPosition = (LutPosition)m_settings.lutPosition;
    if (opts.proxy == 2 || opts.proxy == 4) m_proxy = opts.proxy;
    InitImageIO();
    applyCacheBudget();

    // Start decoding the first frames before any window/GL/OCIO work.
    // A slow drive opens it after the window is up; a command-line export waits for it.
    if (!initialPath.empty()) {
        openPath(initialPath, true, [this, opts, fpsOverride, autoplay] {
            applyStartupLayers(opts);
            if (fpsOverride > 0) m_fps = fpsOverride;
            if (autoplay && frameCount() > 1) setPlaying(1);
        }, !opts.exportPath.empty());
        // Explorer launches one process per selected file: if another instance
        // already has this sequence, bring it forward and quit.
        if (m_seqOpenElsewhere) {
            activateOtherInstance();
            return 0;
        }
    }
    Log("sequence opened (%d frames)", frameCount());

    // OCIO config + GPU processor are built on a worker while the GL driver
    // initializes (the slowest part of startup). The main thread does not touch
    // color state until the future is joined.
    m_color.display = opts.display.empty() ? m_settings.display : opts.display;
    m_color.view = opts.view.empty() ? m_settings.view : opts.view;
    m_color.look = m_settings.look;
    auto colorInit = std::async(std::launch::async, [this, opts] {
        const bool overridden = !opts.config.empty() || !opts.display.empty() || !opts.view.empty();
        loadConfig(opts.config.empty() ? m_settings.configSource : opts.config, !overridden);
        m_prebuiltGpu = m_color.buildGpuProcessor();
        Log("OCIO config loaded: %s", m_color.source().c_str());
    });

    if (!createWindow()) return 1;
    // Show immediately (dark background) so the app feels instant.
    ShowWindow(m_hwnd, m_settings.winMaximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL);
    UpdateWindow(m_hwnd);
    if (!createGLContext()) {
        colorInit.wait();
        MessageBoxW(m_hwnd, L"OpenGL 4.1 is required.", APP_NAME_W, MB_ICONERROR);
        return 1;
    }
    Log("GL context: %s | %s", (const char*)glGetString(GL_VERSION), (const char*)glGetString(GL_RENDERER));
    initImGui();
    Log("imgui ready");
    std::string err;
    if (!m_viewer.init(err, m_hdc, m_glrc)) {
        colorInit.wait();
        MessageBoxW(m_hwnd, FromUtf8("Shader error:\n" + err).c_str(), APP_NAME_W, MB_ICONERROR);
        return 1;
    }
    m_cache.setOnFrameReady([hwnd = m_hwnd] { PostMessageW(hwnd, WM_APP_FRAME_READY, 0, 0); });
    colorInit.wait();
    updateTitle();
    m_ready = true;
    if (!opts.exportPath.empty()) {
        m_cliExport = opts;
        m_batchMode = true;
    }
    // --denoise oidn|oidn-cpu|oidn-gpu|optix|optix-temporal: the export or batch output is denoised
    // (with --export / --batch only: those runs never save the settings it changes).
    bool cliFailed = false;
    if (!opts.denoise.empty() && opts.exportPath.empty() && opts.batchRoot.empty()) Log("--denoise needs --export or --batch: ignored");
    else if (!opts.denoise.empty()) {
        const std::string& d = opts.denoise;
        const bool optix = d.rfind("optix", 0) == 0;
        m_settings.denoiseEngine = optix ? 1 : 0;
        m_settings.optixTemporal = d == "optix-temporal";
        if (d == "oidn-cpu") m_settings.oidnDevice = 1;
        else if (d == "oidn-gpu") m_settings.oidnDevice = 2;
        if (!optix && !OidnInstalled()) {
            // Never downloaded without asking: the first download happens in the Filters panel.
            Log("denoise: Open Image Denoise is not installed; turn the denoise on once in the Filters panel to download it");
            cliFailed = true;
            m_cliExport = StartupOptions();
            m_exitCode = 1;
            PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
        } else if (!opts.batchRoot.empty()) {
            m_settings.batchDenoise = true;
        } else {
            setDenoise(true);
        }
    }
    // Debug: SP_TEST_OPEN=about|batch|settings|crypto|stack|filters|replace opens a dialog/panel at startup (with SP_DUMP for
    // screenshots); SP_TEST_LUT=<file> loads a LUT; SP_TEST_DENOISE=oidn|optix|optix-temporal turns the denoise on.
    if (const wchar_t* l = _wgetenv(L"SP_TEST_LUT")) setLut(ToUtf8(l));
    if (const wchar_t* d = _wgetenv(L"SP_TEST_DENOISE")) {
        m_settings.denoiseEngine = std::wstring(d).rfind(L"optix", 0) == 0 ? 1 : 0;
        m_settings.optixTemporal = std::wstring(d) == L"optix-temporal";
        setDenoise(true);
    }
    if (const wchar_t* t = _wgetenv(L"SP_TEST_OPEN")) {
        const std::wstring w = t;
        if (w == L"about") m_openAbout = true;
        if (w == L"replace" && m_seq) { m_pendingOpen = initialPath; m_openReplace = true; }
        if (w == L"crypto") m_cryptoPanel = true;
        if (w == L"stack") m_stackPanel = true;
        if (w == L"settings") m_openSettings = true;
        if (w == L"info") m_infoPanel = true;
        if (w == L"filters") m_filterPanel = true;
        if (w == L"scopes") m_scopesOpen = true;
        if (w == L"report" || w == L"reportcheck") m_reportOpen = true;
        if (w == L"reportcheck") startFrameCheck();
        if (w == L"batch") {
            openBatchDialog();
            batchAddPaths({ GetParentDir(initialPath) + L"\\.." });
        }
    }
    stackTestSetup();
    // Debug: SP_TEST_COMPARE=<file> (SP_TEST_COMPARE_MODE=0..3), SP_TEST_CHECK=1..3, SP_TEST_GUIDE=<aspect index>,
    // SP_TEST_VERSION=+1/-1, SP_TEST_OPEN=info|scopes|report|reportcheck.
    if (const wchar_t* c = _wgetenv(L"SP_TEST_COMPARE")) {
        setCompare(c);
        if (const wchar_t* m = _wgetenv(L"SP_TEST_COMPARE_MODE")) m_cmpMode = (CompareMode)std::clamp(_wtoi(m), 0, (int)CompareMode::Count - 1);
    }
    if (const wchar_t* c = _wgetenv(L"SP_TEST_CHECK")) m_check = (CheckMode)std::clamp(_wtoi(c), 0, 3);
    if (const wchar_t* g = _wgetenv(L"SP_TEST_GUIDE")) {
        m_settings.guideAspect = std::clamp(_wtoi(g), 0, AspectCount() - 1);
        m_settings.guideSafe = m_settings.guideThirds = m_settings.guideCenter = true;
    }
    if (const wchar_t* v = _wgetenv(L"SP_TEST_VERSION")) switchVersion(_wtoi(v));
    if (!opts.batchRoot.empty() && !cliFailed) {
        m_batchMode = true;           // settings below are for this run only
        m_quitAfterBatch = true;
        openBatchDialog();
        m_openBatch = false;
        m_batchOpt.aspect = ParseAspect(opts.aspect);   // only what the command line asks for
        m_batchOpt.aspectBars = opts.aspectBars;
        m_batchOpt.burnIn = ParseBurnIn(opts.burnIn, opts.burnText);
        m_settings.batchRecursive = true;
        m_settings.batchSkipExisting = !opts.overwrite;
        m_settings.batchDest = opts.batchOutDir.empty() ? 0 : 1;
        m_settings.batchFolder = ToUtf8(opts.batchOutDir);
        if (!opts.batchOutDir.empty()) CreateDirectoryW(opts.batchOutDir.c_str(), nullptr);
        static const struct { const char* name; ExportCodec codec; } codecs[] = {
            { "h264", ExportCodec::H264 }, { "h265", ExportCodec::H265 }, { "prores-proxy", ExportCodec::ProRes422Proxy },
            { "prores-lt", ExportCodec::ProRes422LT }, { "prores", ExportCodec::ProRes422 }, { "prores-hq", ExportCodec::ProRes422HQ },
            { "prores-4444", ExportCodec::ProRes4444 },
        };
        for (auto& c : codecs)
            if (opts.exportCodec == c.name) m_batchOpt.codec = c.codec;
        m_batchOpt.hardware = opts.exportHardware;
        m_batchOpt.alpha = opts.exportAlpha;
        m_batchOpt.premultiplied = opts.exportPremultiplied;
        if (fpsOverride > 0) m_batchOpt.fps = fpsOverride;
        batchAddPaths({ opts.batchRoot });
        if (m_batch.empty()) {
            Log("batch: no sequences found in %s", ToUtf8(opts.batchRoot).c_str());
            m_exitCode = 1;
            PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
        } else {
            startBatch();
        }
    }
    frame();
    Log("first frame presented");
    if (!m_batchMode && m_settings.checkUpdates && std::time(nullptr) - m_settings.lastUpdateCheck >= 24 * 3600)
        startUpdateCheck(false);
    startWatching();   // after the first frame: live refresh costs nothing at startup

    bool quit = false;
    while (!quit) {
        const bool animating = m_playDir != 0 || m_scrubbing || m_renderFrames > 0 || m_exporter || m_batchRunning;
        if (!animating) {
            const bool poll = m_toastUntil > Now() || m_refreshFirst > 0.0 || m_opening;
            const DWORD timeout = poll || _wtoi(_wgetenv(L"SP_DUMP") ? _wgetenv(L"SP_DUMP") : L"0") > 1 ? 100 : INFINITE;
            MsgWaitForMultipleObjectsEx(0, nullptr, timeout, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            m_renderFrames = 1;
        }
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) quit = true;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            m_renderFrames = 3;   // ImGui needs a few frames to settle after input
        }
        if (quit) break;
        pollOpen();
        frame();
        runDeferred();
        if (m_renderFrames > 0) --m_renderFrames;
    }
    shutdown();
    return m_exitCode;
}

bool App::createWindow()
{
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.style = CS_OWNDC | CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hIconSm = wc.hIcon;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(19, 19, 21));
    wc.lpszClassName = L"SequencePlayerWindow";
    RegisterClassExW(&wc);

    int x = m_settings.winX, y = m_settings.winY, w = m_settings.winW, h = m_settings.winH;
    // Reject positions that are no longer on any monitor.
    RECT r{ x, y, x + w, y + h };
    if (x == -1 || !MonitorFromRect(&r, MONITOR_DEFAULTTONULL)) x = y = CW_USEDEFAULT;

    m_hwnd = CreateWindowExW(WS_EX_ACCEPTFILES, wc.lpszClassName, APP_NAME_W, WS_OVERLAPPEDWINDOW,
                             x, y, w, h, nullptr, nullptr, inst, nullptr);
    if (!m_hwnd) return false;

    BOOL dark = TRUE;
    DwmSetWindowAttribute(m_hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));
    COLORREF caption = RGB(19, 19, 21);
    DwmSetWindowAttribute(m_hwnd, 35 /*DWMWA_CAPTION_COLOR*/, &caption, sizeof(caption));
    m_dpiScale = GetDpiForWindow(m_hwnd) / 96.0f;
    return true;
}

bool App::createGLContext()
{
    m_hdc = GetDC(m_hwnd);
    PIXELFORMATDESCRIPTOR pfd{ sizeof(pfd), 1 };
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cAlphaBits = 8;
    const int pf = ChoosePixelFormat(m_hdc, &pfd);
    if (!pf || !SetPixelFormat(m_hdc, pf, &pfd)) return false;

    HGLRC legacy = wglCreateContext(m_hdc);
    if (!legacy) return false;
    wglMakeCurrent(m_hdc, legacy);

    using CreateCtxFn = HGLRC(WINAPI*)(HDC, HGLRC, const int*);
    auto createCtx = (CreateCtxFn)wglGetProcAddress("wglCreateContextAttribsARB");
    if (createCtx) {
        const int attribs[] = {
            0x2091 /*WGL_CONTEXT_MAJOR_VERSION_ARB*/, 4,
            0x2092 /*WGL_CONTEXT_MINOR_VERSION_ARB*/, 1,
            0x9126 /*WGL_CONTEXT_PROFILE_MASK_ARB*/, 0x1 /*CORE*/,
            0,
        };
        HGLRC core = createCtx(m_hdc, nullptr, attribs);
        if (core) {
            wglMakeCurrent(m_hdc, core);
            wglDeleteContext(legacy);
            m_glrc = core;
        }
    }
    if (!m_glrc) m_glrc = legacy;

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) return false;
    while (glGetError() != GL_NO_ERROR) {}
    if (!GLEW_VERSION_4_0) return false;

    using SwapFn = BOOL(WINAPI*)(int);
    if (auto swap = (SwapFn)wglGetProcAddress("wglSwapIntervalEXT")) swap(1);
    return true;
}

void App::initImGui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;

    wchar_t winDir[MAX_PATH];
    GetWindowsDirectoryW(winDir, MAX_PATH);
    const std::wstring fonts = std::wstring(winDir) + L"\\Fonts\\";
    const std::wstring ui = fonts + L"segoeui.ttf", mono = fonts + L"consola.ttf";
    m_fontUI = FileExists(ui) ? io.Fonts->AddFontFromFileTTF(ToUtf8(ui).c_str(), 15.0f) : io.Fonts->AddFontDefault();
    m_fontMono = FileExists(mono) ? io.Fonts->AddFontFromFileTTF(ToUtf8(mono).c_str(), 13.0f) : m_fontUI;

    ImGui_ImplWin32_InitForOpenGL(m_hwnd);
    ImGui_ImplOpenGL3_Init("#version 410");
    applyStyle(m_dpiScale);
}

void App::applyStyle(float dpi)
{
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();
    ImGui::StyleColorsDark(&s);
    s.WindowPadding = ImVec2(10, 8);
    s.FramePadding = ImVec2(8, 5);
    s.ItemSpacing = ImVec2(8, 6);
    s.ItemInnerSpacing = ImVec2(6, 4);
    s.WindowRounding = 0;
    s.ChildRounding = 6;
    s.FrameRounding = 6;
    s.PopupRounding = 8;
    s.GrabRounding = 6;
    s.ScrollbarRounding = 6;
    s.ScrollbarSize = 10;
    s.GrabMinSize = 8;
    s.WindowBorderSize = 0;
    s.PopupBorderSize = 1;
    s.FrameBorderSize = 0;
    s.SeparatorTextBorderSize = 1;

    const ImVec4 panel(0.105f, 0.105f, 0.117f, 1);
    const ImVec4 frame(0.155f, 0.155f, 0.172f, 1), hover(0.205f, 0.205f, 0.228f, 1), active(0.25f, 0.25f, 0.28f, 1);
    const ImVec4 accent(0.29f, 0.56f, 1.0f, 1), accentDim(0.29f, 0.56f, 1.0f, 0.35f);
    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = ImVec4(0.90f, 0.90f, 0.92f, 1);
    c[ImGuiCol_TextDisabled] = ImVec4(0.52f, 0.52f, 0.57f, 1);
    c[ImGuiCol_WindowBg] = panel;
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = ImVec4(0.12f, 0.12f, 0.135f, 1.0f);
    c[ImGuiCol_Border] = ImVec4(1, 1, 1, 0.08f);
    c[ImGuiCol_FrameBg] = frame;
    c[ImGuiCol_FrameBgHovered] = hover;
    c[ImGuiCol_FrameBgActive] = active;
    c[ImGuiCol_Button] = frame;
    c[ImGuiCol_ButtonHovered] = hover;
    c[ImGuiCol_ButtonActive] = active;
    c[ImGuiCol_Header] = accentDim;
    c[ImGuiCol_HeaderHovered] = hover;
    c[ImGuiCol_HeaderActive] = active;
    c[ImGuiCol_CheckMark] = accent;
    c[ImGuiCol_SliderGrab] = accent;
    c[ImGuiCol_SliderGrabActive] = accent;
    c[ImGuiCol_Separator] = ImVec4(1, 1, 1, 0.08f);
    c[ImGuiCol_TitleBg] = ImVec4(0.145f, 0.145f, 0.162f, 1);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.145f, 0.145f, 0.162f, 1);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.55f);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_NavCursor] = ImVec4(0, 0, 0, 0);
    s.ScaleAllSizes(dpi);
    s.FontScaleDpi = dpi;
}

void App::shutdown()
{
    m_settings.loopMode = (int)m_loop;
    if (!m_batchMode) m_settings.save();

    m_cache.setOnFrameReady(nullptr);
    m_watcher.stop();
    m_frameCheck.reset();
    if (m_seqMutex) CloseHandle(m_seqMutex);
    if (m_logoTex) glDeleteTextures(1, &m_logoTex);
    for (unsigned* t : { &m_waveTex, &m_vectorTex })
        if (*t) glDeleteTextures(1, t);
    m_viewer.shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    wglMakeCurrent(nullptr, nullptr);
    if (m_glrc) wglDeleteContext(m_glrc);
    if (m_hdc) ReleaseDC(m_hwnd, m_hdc);
    g_app = nullptr;
}

long long App::handleMessage(HWND hwnd, unsigned msg, unsigned long long wParam, long long lParam, bool& handled)
{
    handled = true;
    switch (msg) {
    case WM_SIZE:
        if (m_ready && !m_inFrame && wParam != SIZE_MINIMIZED) frame();
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        if (m_ready && !m_inFrame) frame();
        return 0;
    }
    case WM_ERASEBKGND:
        if (!m_ready) { handled = false; return 0; }   // class brush until GL takes over
        return 1;
    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize.x = LONG(640 * m_dpiScale);
        mmi->ptMinTrackSize.y = LONG(400 * m_dpiScale);
        return 0;
    }
    case WM_DPICHANGED: {
        m_dpiScale = HIWORD(wParam) / 96.0f;
        const RECT* r = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        if (ImGui::GetCurrentContext()) applyStyle(m_dpiScale);
        return 0;
    }
    case WM_DROPFILES: {
        HDROP drop = (HDROP)wParam;
        std::vector<std::wstring> paths;
        const UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < n; ++i) {
            wchar_t path[MAX_PATH * 4];
            if (DragQueryFileW(drop, i, path, (UINT)std::size(path))) paths.push_back(path);
        }
        DragFinish(drop);
        if (m_batchRunning || m_exporter || paths.empty()) return 0;
        // With the stack panel open, dropped sequences become layers.
        if (m_stackPanel && m_seq && !m_batchDialogOpen) {
            stackAddSequences(paths);
            m_renderFrames = 3;
            return 0;
        }
        // Several items, or the batch dialog is open: queue them for conversion.
        if (m_batchDialogOpen || paths.size() > 1) {
            if (!m_batchDialogOpen) openBatchDialog();
            batchAddPaths(paths);
            m_renderFrames = 3;
        } else {
            requestOpen(paths[0]);
        }
        return 0;
    }
    case WM_APP_FRAME_READY:
        return 0;
    case kDirChangedMsg: {
        // Wait for a quiet moment: a frame being written sends many notices.
        const double now = Now();
        if (m_refreshFirst <= 0.0) m_refreshFirst = now;
        m_refreshDue = now + 0.4;
        return 0;
    }
    case WM_SYSCOMMAND:
        // Alt alone would enter the (empty) window menu and swallow the next key (Alt+Up / Down).
        if ((wParam & 0xFFF0) == SC_KEYMENU && lParam == 0) return 0;
        break;
    case WM_CLOSE: {
        // Save placement while the window still exists.
        WINDOWPLACEMENT wp{ sizeof(wp) };
        if (!m_fullscreen && GetWindowPlacement(hwnd, &wp)) {
            m_settings.winX = wp.rcNormalPosition.left;
            m_settings.winY = wp.rcNormalPosition.top;
            m_settings.winW = wp.rcNormalPosition.right - wp.rcNormalPosition.left;
            m_settings.winH = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top;
            m_settings.winMaximized = wp.showCmd == SW_SHOWMAXIMIZED;
        }
        DestroyWindow(hwnd);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    handled = false;
    return 0;
}

// ---------------------------------------------------------------------------
// Actions

void App::applyLanguage()
{
    const std::string& l = m_settings.language;
    SetLanguage(l == "it" ? Lang::Italian : l == "en" ? Lang::English
                : IsSystemLanguageItalian() ? Lang::Italian : Lang::English);
}

void App::applyCacheBudget()
{
    uint64_t bytes;
    if (m_settings.cacheMB > 0) bytes = uint64_t(m_settings.cacheMB) << 20;
    else bytes = std::min<uint64_t>(GetPhysicalMemoryBytes() * 4 / 10, uint64_t(16) << 30);
    m_cache.setBudget((size_t)bytes);
}

// The disk work of an open. Reading the header of an online-only file (OneDrive, Dropbox,
// Google Drive) downloads the whole file first, so it never runs on the UI thread unbounded.
struct App::OpenState {
    std::wstring path;
    bool addToRecent = true;
    bool startup = false;
    std::function<void()> then;
    HWND notify = nullptr;
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    // results
    Sequence seq;
    int start = 0;
    ExrInfo exr;
};

void App::openPath(const std::wstring& path, bool addToRecent, std::function<void()> then, bool wait)
{
    auto st = std::make_shared<OpenState>();
    st->path = path;
    st->addToRecent = addToRecent;
    st->startup = !m_ready;
    st->then = std::move(then);
    st->notify = m_hwnd;
    m_opening = st;   // supersedes an open still running
    // Detached: the state is shared, and a stuck download must not hold the UI or the exit.
    std::thread([st] {
        int start = 0;
        Sequence seq = DetectSequence(st->path, &start);
        ExrInfo exr;
        if (!seq.empty() && GetFileExtension(seq.frames[0].path) == L".exr") exr = ReadExrInfo(seq.frames[start].path);
        {
            std::lock_guard lock(st->mutex);
            st->seq = std::move(seq);
            st->start = start;
            st->exr = std::move(exr);
            st->done = true;
        }
        st->cv.notify_all();
        if (st->notify) PostMessageW(st->notify, WM_NULL, 0, 0);   // wake the idle message loop
    }).detach();

    // A local disk answers well within this: the open completes right here, as it always did.
    {
        std::unique_lock lock(st->mutex);
        if (wait) st->cv.wait(lock, [&] { return st->done; });
        else st->cv.wait_for(lock, std::chrono::milliseconds(150), [&] { return st->done; });
    }
    if (st->done) {
        pollOpen();
        return;
    }
    Log("open: waiting for %s", ToUtf8(path).c_str());
    showToast(std::string(tr(S::Opening)) + ": " + ToUtf8(GetFileName(path)));
    m_toastUntil = Now() + 24 * 3600.0;   // until the open ends (keeps the idle loop polling)
}

void App::pollOpen()
{
    if (!m_opening) return;
    {
        std::lock_guard lock(m_opening->mutex);
        if (!m_opening->done) return;
    }
    const auto st = std::move(m_opening);
    m_opening.reset();
    m_renderFrames = 3;
    if (m_toastUntil > Now() + 3600.0) m_toastUntil = 0.0;   // the "opening" toast
    const std::wstring& path = st->path;
    Sequence& seq = st->seq;
    const int start = st->start;
    if (seq.empty()) {
        showToast(std::string(tr(S::LoadError)) + ": " + ToUtf8(GetFileName(path)), true);
        return;
    }
    if (!IsSupportedExtension(GetFileExtension(seq.frames[0].path))) {
        showToast(std::string(tr(S::LoadError)) + ": " + ToUtf8(GetFileName(path)), true);
        return;
    }
    m_seq = std::make_shared<Sequence>(std::move(seq));
    m_seqExt = GetFileExtension(m_seq->frames[0].path);
    m_stack.clear();      // stack layers belong to the previous shot
    m_stackSel = 0;
    m_shownSet.reset();

    // EXR layers: keep the previously viewed layer when the new shot has it.
    const std::string prevLayer = currentLayerLabel();
    m_exrInfo = std::move(st->exr);
    m_layer = m_exrInfo.defaultLayer;
    for (size_t i = 0; i < m_exrInfo.layers.size(); ++i)
        if (m_exrInfo.layers[i].label == prevLayer) m_layer = (int)i;
    m_crypto = 0;
    m_cryptoSel.clear();
    m_lastPick.clear();
    m_alphaOverride = -1;
    m_matteSeq.reset();   // an external matte belongs to the previous shot
    m_matteInfo = ExrInfo();
    m_matteFiles.reset();
    m_matteMissing = 0;
    resetCompare();       // so does the compared sequence
    m_badImage.reset();
    m_frameCheck.reset();
    if (!hasCrypto()) { m_matte = MatteMode::Off; m_cryptoPanel = false; }
    // A Cryptomatte-only file (no color channels) would be black: show its IDs.
    if (m_exrInfo.layers.empty() && hasCrypto() && m_matte == MatteMode::Off) { m_matte = MatteMode::Ids; m_cryptoPanel = true; }
    applyLoadOptions();
    m_index = start;
    m_in = 0;
    m_out = m_seq->count() - 1;
    m_playDir = 0;
    m_shown.reset();
    m_fit = true;
    m_cache.setSequence(m_seq);
    m_cache.setPlayhead(m_index, 1, m_in, m_out, m_loop != LoopMode::Once);
    m_fps = m_settings.defaultFps;

    m_inputUserChosen = false;
    chooseInputForSequence();
    claimSequence();
    if (st->addToRecent) Settings::PushRecent(m_settings.recentFiles, ToUtf8(path));
    updateTitle();
    // Opened after the window came up: the duplicate check of run() is done here.
    if (st->startup && m_seqOpenElsewhere && m_hwnd) {
        activateOtherInstance();
        PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
        return;
    }
    if (st->then) st->then();
    if (m_ready) startWatching();   // at startup: once the first frame is up
}

void App::requestOpen(const std::wstring& path)
{
    if (m_seq && m_settings.confirmReplace && !m_batchMode) {
        m_pendingOpen = path;
        m_openReplace = true;
        return;
    }
    openPath(path);
}

std::vector<const char*> App::workToLose() const
{
    std::vector<const char*> w;
    if (!m_seq) return w;
    if (stackActive()) w.push_back(tr(S::ReplaceStack));
    if (m_in > 0 || m_out < frameCount() - 1) w.push_back(tr(S::ReplaceInOut));
    if (!m_cryptoSel.empty()) w.push_back(tr(S::ReplaceCrypto));
    if (m_matteSeq) w.push_back(tr(S::ReplaceMatte));
    if (m_alphaOverride >= 0) w.push_back(tr(S::ReplaceAlpha));
    if (compareActive()) w.push_back(tr(S::ReplaceCompare));
    return w;
}

void App::claimSequence()
{
    if (m_seqMutex) CloseHandle(m_seqMutex);
    m_seqMutex = nullptr;
    m_seqOpenElsewhere = false;
    if (!m_seq) return;
    const std::wstring key = ToLower(m_seq->directory + L"\\" + m_seq->displayName());
    wchar_t name[64];
    swprintf(name, 64, L"Local\\SequencePlayer.Seq.%016llx", (unsigned long long)std::hash<std::wstring>{}(key));
    m_seqMutex = CreateMutexW(nullptr, FALSE, name);
    m_seqOpenElsewhere = m_seqMutex && GetLastError() == ERROR_ALREADY_EXISTS;
}

void App::activateOtherInstance()
{
    struct Ctx { std::wstring title; HWND self; HWND found; } ctx{ m_seq->displayName(), m_hwnd, nullptr };
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        auto* c = reinterpret_cast<Ctx*>(lp);
        wchar_t cls[64], title[512];
        if (h == c->self || !GetClassNameW(h, cls, 64) || wcscmp(cls, L"SequencePlayerWindow") != 0) return TRUE;
        GetWindowTextW(h, title, 512);
        if (wcsncmp(title, c->title.c_str(), c->title.size()) == 0) { c->found = h; return FALSE; }
        return TRUE;
    }, (LPARAM)&ctx);
    if (ctx.found) {
        if (IsIconic(ctx.found)) ShowWindow(ctx.found, SW_RESTORE);
        SetForegroundWindow(ctx.found);
    }
}

void App::openDialog(bool folder)
{
    std::wstring p = ShowOpenFileDialog(m_hwnd, folder, FromUtf8(folder ? tr(S::OpenFolder) : tr(S::Open)).c_str());
    if (!p.empty()) requestOpen(p);
}

void App::loadConfig(const std::string& source, bool persist)
{
    m_persistColor = persist;
    const std::string src = source.empty() ? ColorManager::DefaultBuiltinUri() : source;
    if (!m_color.load(src)) {
        showToast(std::string(tr(S::ConfigError)) + ": " + m_color.error(), true);
        if (!m_color.valid()) m_color.load(ColorManager::DefaultBuiltinUri());
        if (!m_color.valid()) return;
    } else {
        if (persist) {
            m_settings.configSource = src;
            if (m_color.isCustomFile()) Settings::PushRecent(m_settings.recentConfigs, src);
        }
    }
    if (!m_inputUserChosen || !m_color.hasColorSpace(m_color.input)) {
        m_inputUserChosen = false;
        chooseInputForSequence();
    }
    stackRefreshInputs();
    if (compareActive() && !m_cmpInput.empty() && !m_color.hasColorSpace(m_cmpInput))
        m_cmpInput = autoInput(m_cmpIsFloat, m_cmpSeq->frames[0].path);
    m_colorDirty = true;
}

void App::loadCustomConfigDialog()
{
    std::wstring p = ShowOpenOcioDialog(m_hwnd, FromUtf8(tr(S::LoadCustomConfig)).c_str());
    if (!p.empty()) loadConfig(ToUtf8(p));
}

void App::loadLutDialog()
{
    std::wstring p = ShowOpenFilteredDialog(m_hwnd, FromUtf8(tr(S::LoadLut)).c_str(), L"LUT",
                                            L"*.cube;*.3dl;*.csp;*.spi1d;*.spi3d;*.spimtx;*.clf;*.ctf;*.cc;*.ccc;*.cdl;*.cub;*.itx;*.look;*.mga;*.m3d;*.vf;*.hdl;*.lut;*.1dl");
    if (!p.empty()) setLut(ToUtf8(p));
}

void App::setLut(const std::string& path)
{
    if (!path.empty() && !m_color.checkLut(path)) {
        showToast(std::string(tr(S::LutError)) + ": " + m_color.error(), true);
        auto& r = m_settings.recentLuts;
        r.erase(std::remove(r.begin(), r.end(), path), r.end());
        return;
    }
    m_color.lutPath = path;
    if (!path.empty()) Settings::PushRecent(m_settings.recentLuts, path);
    m_prebuiltGpu.reset();   // built without it
    m_colorDirty = true;
}

void App::chooseInputForSequence()
{
    if (!m_color.valid()) return;
    m_color.input = autoInput(m_seq ? IsFloatFormat(m_seqExt) : true, m_seq ? m_seq->frames[0].path : std::wstring());
    m_colorDirty = true;
}

std::string App::autoInput(bool isFloat, const std::wstring& path) const
{
    auto it = m_settings.inputMemory.find(m_color.source() + (isFloat ? "|float" : "|int"));
    if (it != m_settings.inputMemory.end() && !it->second.empty() && m_color.hasColorSpace(it->second)) return it->second;
    return m_color.defaultInput(isFloat, ToUtf8(path));
}

void App::setInput(const std::string& name, bool userChoice)
{
    const bool isFloat = m_seq ? IsFloatFormat(m_seqExt) : true;
    std::string& remembered = m_settings.inputMemory[m_color.source() + (isFloat ? "|float" : "|int")];
    if (name.empty()) {            // "Auto"
        remembered.clear();
        m_inputUserChosen = false;
        chooseInputForSequence();
        return;
    }
    m_color.input = name;
    if (userChoice) {
        remembered = name;
        m_inputUserChosen = true;
    }
    m_colorDirty = true;
}

void App::setPlaying(int direction)
{
    if (direction != 0 && m_loop == LoopMode::Once) {
        if (direction > 0 && m_index >= m_out) m_index = m_in;
        if (direction < 0 && m_index <= m_in) m_index = m_out;
    }
    m_playDir = direction;
    m_nextTick = Now() + 1.0 / m_fps;
    m_lastFrameTime = 0.0;
    m_actualFps = m_fps;
}

void App::step(int delta)
{
    if (!frameCount()) return;
    m_playDir = 0;
    const int n = frameCount();
    m_index = ((m_index + delta) % n + n) % n;
}

void App::seek(int index)
{
    if (!frameCount()) return;
    m_index = std::clamp(index, 0, frameCount() - 1);
}

void App::toggleFullscreen()
{
    if (!m_fullscreen) {
        WINDOWPLACEMENT wp{ sizeof(wp) };
        GetWindowPlacement(m_hwnd, &wp);
        m_savedMaximized = wp.showCmd == SW_SHOWMAXIMIZED;
        RECT r;
        GetWindowRect(m_hwnd, &r);
        m_savedRect = { r.left, r.top, r.right, r.bottom };
        m_savedStyle = GetWindowLongW(m_hwnd, GWL_STYLE);
        MONITORINFO mi{ sizeof(mi) };
        GetMonitorInfoW(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), &mi);
        SetWindowLongW(m_hwnd, GWL_STYLE, (m_savedStyle & ~WS_OVERLAPPEDWINDOW) | WS_POPUP);
        SetWindowPos(m_hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
        m_fullscreen = true;
    } else {
        SetWindowLongW(m_hwnd, GWL_STYLE, m_savedStyle);
        SetWindowPos(m_hwnd, nullptr, m_savedRect.left, m_savedRect.top, m_savedRect.right - m_savedRect.left,
                     m_savedRect.bottom - m_savedRect.top, SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOOWNERZORDER);
        if (m_savedMaximized) ShowWindow(m_hwnd, SW_MAXIMIZE);
        m_fullscreen = false;
    }
    m_fit = true;
}

void App::showToast(const std::string& text, bool error)
{
    m_toast = text;
    m_toastError = error;
    m_toastUntil = Now() + (error ? 6.0 : 2.5);
}

void App::updateTitle()
{
    if (!m_hwnd) return;
    std::wstring t = APP_NAME_W;
    if (m_seq) t = m_seq->displayName() + L"  —  " + t;
    SetWindowTextW(m_hwnd, t.c_str());
}

// ---------------------------------------------------------------------------
// Per-frame

int App::nextIndex(int from, int dir, bool& reverse) const
{
    reverse = false;
    if (from < m_in || from > m_out) return dir > 0 ? m_in : m_out;
    int n = from + dir;
    if (n >= m_in && n <= m_out) return n;
    switch (m_loop) {
    case LoopMode::Loop: return dir > 0 ? m_in : m_out;
    case LoopMode::Once: return -1;
    case LoopMode::PingPong:
        reverse = true;
        n = from - dir;
        return std::clamp(n, m_in, m_out);
    }
    return -1;
}

void App::updatePlayback()
{
    const double now = Now();
    // Proxy changed, or an export started / ended: decode at the right resolution.
    if (m_seq && (m_exporter || m_batchRunning ? 1 : m_proxy) != m_planProxy) applyLoadPlan();
    if (m_playDir != 0 && frameCount() > 0) {
        const double period = 1.0 / std::max(0.1, m_fps);
        if (now >= m_nextTick) {
            bool reverse = false;
            const int next = nextIndex(m_index, m_playDir, reverse);
            if (next < 0) {
                m_playDir = 0;
            } else if (m_cache.get(next)) {   // every layer decoded
                if (reverse) m_playDir = -m_playDir;
                m_index = next;
                m_nextTick += period;
                if (now - m_nextTick > period) m_nextTick = now + period;   // fell behind: resync
                if (m_lastFrameTime > 0) {
                    const double inst = 1.0 / std::max(1e-6, now - m_lastFrameTime);
                    m_actualFps = m_actualFps * 0.9 + inst * 0.1;
                }
                m_lastFrameTime = now;
            } else {
                m_nextTick = now;   // frame not decoded yet: hold
            }
        }
    }
    if (m_exporter) {
        m_playDir = 0;
        m_cache.setPlayhead(m_exportNext, 1, m_exportFirst, m_exportLast, false);
        if (FrameSetPtr cur = m_cache.get(m_index)) showFrame(cur);
    } else if (frameCount() > 0) {
        m_cache.setPlayhead(m_index, m_playDir < 0 ? -1 : 1, m_in, m_out, m_loop != LoopMode::Once);
        if (FrameSetPtr cur = m_cache.get(m_index)) showFrame(cur);
    }
    static double lastFpsLog = 0.0;
    if (m_playDir != 0 && now - lastFpsLog > 1.0) {
        lastFpsLog = now;
        static int lastRendered = 0;
        Log("playing frame %d  fps %.2f (target %.3f)  renders/s %d  cache %zu MB  viewer max %.1f ms", m_index, m_actualFps, m_fps,
            m_framesRendered - lastRendered, m_cache.usedBytes() >> 20, m_viewerMsMax);
        lastRendered = m_framesRendered;
        m_viewerMsMax = 0.0;
    }
    if (now - m_lastMaskUpdate > 0.1) {
        m_cache.stateMask(m_cacheMask);
        m_lastMaskUpdate = now;
    }
}

void App::rebuildColorIfNeeded()
{
    if (!m_colorDirty) return;
    m_colorDirty = false;
    std::string err;
    m_colorError.clear();
    const bool managed = m_colorManaged && m_color.valid();
    std::string source = m_color.input;
    if (stackActive()) {
        // Each layer is converted to the working space and blended there; the view runs once.
        m_stackInputs.clear();
        for (const StackLayer& s : m_stack)
            if (std::find(m_stackInputs.begin(), m_stackInputs.end(), s.input) == m_stackInputs.end()) m_stackInputs.push_back(s.input);
        std::vector<OCIO::ConstGPUProcessorRcPtr> procs(m_stackInputs.size());
        if (managed) {
            source = m_color.workingSpace();
            if (source.empty()) source = m_stack.front().input;
            for (size_t i = 0; i < procs.size(); ++i)
                if (!(procs[i] = m_color.buildConversion(m_stackInputs[i], source)) && m_colorError.empty()) m_colorError = m_color.error();
        }
        if (!m_viewer.setInputTransforms(procs, err) && m_colorError.empty()) m_colorError = err;
        m_prebuiltGpu.reset();   // built for the single view
    }
    // A/B compare: B goes through the same view from its own input space.
    if (compareActive()) {
        std::string errB;
        m_viewer.setProcessorB(managed ? m_color.buildGpuProcessor(compareInput()) : m_color.buildLutProcessor(), errB);
    }
    if (!managed) {
        // Only the LUT, when one is loaded, on the file values.
        const auto lut = m_color.buildLutProcessor();
        if (!lut && !m_color.lutPath.empty()) m_colorError = m_color.error();
        if (!m_viewer.setProcessor(lut, err) && m_colorError.empty()) m_colorError = err;
        if (!m_colorError.empty()) showToast(std::string(tr(S::ConfigError)) + ": " + m_colorError, true);
        return;
    }
    const auto gpu = stackActive() ? m_color.buildGpuProcessor(source)
                   : m_prebuiltGpu ? std::exchange(m_prebuiltGpu, nullptr) : m_color.buildGpuProcessor();
    Log("GPU processor %s -> %s / %s: %s", source.c_str(), m_color.display.c_str(), m_color.view.c_str(), gpu ? "ok" : m_color.error().c_str());
    if (!gpu) {
        m_colorError = m_color.error();
        m_viewer.setProcessor(nullptr, err);
    } else if (!m_viewer.setProcessor(gpu, err)) {
        m_colorError = err;
    }
    if (!m_colorError.empty()) showToast(std::string(tr(S::ConfigError)) + ": " + m_colorError, true);
    if (m_persistColor) {
        m_settings.display = m_color.display;
        m_settings.view = m_color.view;
        m_settings.look = m_color.look;
    }
}

void App::handleShortcuts()
{
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
        (void)0;
        return;
    }
    const bool ctrl = io.KeyCtrl, shift = io.KeyShift;
    auto pressed = [](ImGuiKey k, bool repeat = false) { return ImGui::IsKeyPressed(k, repeat); };

    if (ctrl && pressed(ImGuiKey_O)) { defer([this, shift] { openDialog(shift); }); return; }
    if (ctrl && pressed(ImGuiKey_E)) { openExportDialog(); return; }
    if (ctrl && pressed(ImGuiKey_B)) { openBatchDialog(); return; }
    if (ctrl && pressed(ImGuiKey_C)) { defer([this] { copyFrame(); }); return; }
    if (ctrl && pressed(ImGuiKey_S)) { defer([this] { saveFrameDialog(); }); return; }
    if (ctrl && shift && pressed(ImGuiKey_R)) { revealFrame(); return; }
    if (ctrl && pressed(ImGuiKey_I) && m_seq) {
        m_infoPanel = !m_infoPanel;
        if (m_infoPanel) m_cryptoPanel = m_stackPanel = false;
        return;
    }
    if (ctrl) return;
    if (io.KeyAlt) {
        if (pressed(ImGuiKey_UpArrow)) defer([this] { switchVersion(1); });
        if (pressed(ImGuiKey_DownArrow)) defer([this] { switchVersion(-1); });
        return;
    }

    if (pressed(ImGuiKey_Space)) setPlaying(m_playDir != 0 ? 0 : 1);
    if (pressed(ImGuiKey_L)) setPlaying(1);
    if (pressed(ImGuiKey_J)) setPlaying(-1);
    if (pressed(ImGuiKey_K)) setPlaying(0);
    if (pressed(ImGuiKey_RightArrow, true)) step(shift ? 10 : 1);
    if (pressed(ImGuiKey_LeftArrow, true)) step(shift ? -10 : -1);
    if (pressed(ImGuiKey_Home)) { m_playDir = 0; seek(m_in); }
    if (pressed(ImGuiKey_End)) { m_playDir = 0; seek(m_out); }
    if (pressed(ImGuiKey_I) && frameCount()) { m_in = m_index; m_out = std::max(m_out, m_in); }
    if (pressed(ImGuiKey_O) && frameCount()) { m_out = m_index; m_in = std::min(m_in, m_out); }
    if (pressed(ImGuiKey_U)) { m_in = 0; m_out = std::max(0, frameCount() - 1); }
    if (pressed(ImGuiKey_F)) m_fit = true;
    if (pressed(ImGuiKey_1)) { m_fit = false; m_zoom = 1.0f; m_panX = m_panY = 0; }
    if (pressed(ImGuiKey_2)) { m_fit = false; m_zoom = 2.0f; m_panX = m_panY = 0; }
    if (pressed(ImGuiKey_3)) { m_fit = false; m_zoom = 0.5f; m_panX = m_panY = 0; }

    auto channel = [&](ChannelMode c) { m_channel = m_channel == c ? ChannelMode::RGB : c; };
    if (pressed(ImGuiKey_R)) channel(ChannelMode::Red);
    if (pressed(ImGuiKey_G)) channel(ChannelMode::Green);
    if (pressed(ImGuiKey_B)) channel(ChannelMode::Blue);
    if (pressed(ImGuiKey_A)) channel(ChannelMode::Alpha);
    if (pressed(ImGuiKey_Y)) channel(ChannelMode::Luma);
    if (pressed(ImGuiKey_C)) m_channel = ChannelMode::RGB;

    if (pressed(ImGuiKey_LeftBracket, true) || pressed(ImGuiKey_Minus, true) || pressed(ImGuiKey_KeypadSubtract, true)) m_exposure -= 0.5f;
    if (pressed(ImGuiKey_RightBracket, true) || pressed(ImGuiKey_Equal, true) || pressed(ImGuiKey_KeypadAdd, true)) m_exposure += 0.5f;
    if (pressed(ImGuiKey_Backspace)) { m_exposure = 0.0f; m_gamma = 1.0f; }

    if (compareActive() && pressed(ImGuiKey_W)) m_cmpMode = CompareMode(((int)m_cmpMode + 1) % (int)CompareMode::Count);
    if (compareActive() && pressed(ImGuiKey_X)) m_cmpSwap = !m_cmpSwap;
    if (pressed(ImGuiKey_N)) setCheck(CheckMode::BadPixels);
    if (pressed(ImGuiKey_E)) setCheck(CheckMode::FalseColor);
    if (pressed(ImGuiKey_Z)) setCheck(CheckMode::Zebra);
    if (pressed(ImGuiKey_H) && m_seq) m_scopesOpen = !m_scopesOpen;

    if (pressed(ImGuiKey_Tab)) m_uiVisible = !m_uiVisible;
    if (pressed(ImGuiKey_F11) || pressed(ImGuiKey_Enter)) defer([this] { toggleFullscreen(); });
    if (pressed(ImGuiKey_Escape) && m_fullscreen) defer([this] { toggleFullscreen(); });
}

void App::handleViewerInput(float vx, float vy, float vw, float vh)
{
    ImGuiIO& io = ImGui::GetIO();
    const ImagePtr& img = m_shown;
    const int imgW = img && img->valid() ? img->fullWidth() : 0, imgH = img && img->valid() ? img->fullHeight() : 0;
    const bool sbs = sideBySide();
    const float hw = std::floor(vw / 2);   // side by side: each image gets half of the viewer

    if (m_fit && imgW > 0) {
        m_zoom = std::min((sbs ? hw : vw) / imgW, vh / imgH);
        m_panX = m_panY = 0;
    }

    const ImVec2 m = io.MousePos;
    const bool inside = m.x >= vx && m.x < vx + vw && m.y >= vy && m.y < vy + vh;
    const bool free = !io.WantCaptureMouse;
    const int half = sbs ? (m.x >= vx + hw ? 1 : 0) : -1;

    // A/B wipe: dragging the line moves the split instead of panning.
    const bool wipe = compareActive() && m_cmpMode == CompareMode::Wipe && imgW > 0;
    if (wipe) {
        const ScreenRect r = imageRect(vx, vy, vw, vh);
        const float split = std::floor(r.x0 + (r.x1 - r.x0) * m_wipe);
        const bool onLine = inside && std::fabs(m.x - split) <= 6.0f * m_dpiScale;
        if (free && onLine) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (free && onLine && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) m_wipeDrag = true;
        if (m_wipeDrag) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            if (r.x1 > r.x0) m_wipe = std::clamp((m.x - r.x0) / (r.x1 - r.x0), 0.0f, 1.0f);
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) m_wipeDrag = false;
        }
    } else {
        m_wipeDrag = false;
    }

    // Pan/zoom are kept in "screen, y up" space relative to viewport center.
    const float centerX = half < 0 ? vx + vw * 0.5f : half ? vx + hw + (vw - hw) * 0.5f : vx + hw * 0.5f;
    const float px = m.x - centerX;
    const float py = (vy + vh * 0.5f) - m.y;

    if (free && inside && io.MouseWheel != 0.0f && imgW > 0) {
        const float newZoom = std::clamp(m_zoom * std::pow(1.2f, io.MouseWheel), 0.02f, 64.0f);
        const float ix = (px - m_panX) / m_zoom, iy = (py - m_panY) / m_zoom;
        m_panX = px - ix * newZoom;
        m_panY = py - iy * newZoom;
        m_zoom = newZoom;
        m_fit = false;
    }
    if (free && inside && !m_wipeDrag &&
        (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 1.0f))) {
        m_panX += io.MouseDelta.x;
        m_panY -= io.MouseDelta.y;
        if (io.MouseDelta.x != 0 || io.MouseDelta.y != 0) m_fit = false;
    }

    // Which image is under the cursor: A, or B in the compare modes that show it there.
    m_hoverB = false;
    if (compareActive()) {
        switch (m_cmpMode) {
        case CompareMode::Toggle: m_hoverB = m_cmpSwap; break;
        case CompareMode::SideBySide: m_hoverB = (half == 1) != m_cmpSwap; break;
        case CompareMode::Wipe: {
            const ScreenRect r = imageRect(vx, vy, vw, vh);
            m_hoverB = (m.x >= std::floor(r.x0 + (r.x1 - r.x0) * m_wipe)) != m_cmpSwap;
            break;
        }
        default: break;
        }
    }
    const bool cryptoPicking = m_matte != MatteMode::Off && hasCrypto() && !m_hoverB;
    if (free && inside && !cryptoPicking && !m_wipeDrag && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) defer([this] { toggleFullscreen(); });

    // Pixel under cursor (matches GLViewer::draw placement).
    m_hoverValid = false;
    if (inside && imgW > 0) {
        const ScreenRect r = imageRect(vx, vy, vw, vh, half);
        const int ix = (int)std::floor((m.x - r.x0) / m_zoom);
        const int iy = (int)std::floor((m.y - r.y0) / m_zoom);
        // File values of the image, or of the selected stack layer, or of B; mapped when that
        // was decoded smaller (proxy) or has another resolution.
        ImagePtr src = img;
        if (stackActive()) {
            const StackLayer& s = m_stack[std::clamp(m_stackSel, 0, (int)m_stack.size() - 1)];
            src = stackImage(m_shownSet, s);
            m_hoverLayer = s.name;
        } else if (m_hoverB) {
            src = compareImage(m_shownSet);
            m_hoverLayer = "B";
        }
        if (ix >= 0 && iy >= 0 && ix < imgW && iy < imgH) {
            m_hoverValid = true;
            m_hoverX = ix;
            m_hoverY = iy;
            if (!src || !src->valid() ||
                !SamplePixel(*src, int(int64_t(ix) * src->width / imgW), int(int64_t(iy) * src->height / imgH), m_hoverRGBA))
                std::fill(std::begin(m_hoverRGBA), std::end(m_hoverRGBA), 0.0f);
        }
    }
    // Cryptomatte: a click (not a drag) toggles the object under the cursor.
    if (free && inside && cryptoPicking && m_hoverValid && ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
        io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] < 9.0f * m_dpiScale * m_dpiScale)
        pickCrypto();
}

void App::runDeferred()
{
    auto tasks = std::move(m_deferred);
    m_deferred.clear();
    for (auto& t : tasks) t();
    if (!tasks.empty()) m_renderFrames = 3;
}

static void DumpBackbuffer(int w, int h)
{
    std::vector<uint8_t> px(size_t(w) * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    if (FILE* f = _wfopen((std::wstring(tmp) + L"SequencePlayer_dump.ppm").c_str(), L"wb")) {
        fprintf(f, "P6\n%d %d\n255\n", w, h);
        for (int y = h - 1; y >= 0; --y) fwrite(px.data() + size_t(y) * w * 3, 1, size_t(w) * 3, f);
        fclose(f);
        Log("dumped backbuffer %dx%d", w, h);
    }
}

void App::frame()
{
    if (m_inFrame) return;
    m_inFrame = true;
    wglMakeCurrent(m_hdc, m_glrc);
    if (m_refreshFirst > 0.0 && (Now() >= m_refreshDue || Now() - m_refreshFirst > 1.5)) refreshSequence();
    rebuildColorIfNeeded();
    m_viewer.setExposure(m_exposure);
    m_viewer.setGamma(m_gamma);
    updatePlayback();
    processBatch();
    processExport();
    const double viewerStart = Now();
    if (stackActive() && m_shown) m_viewer.setComposite(compLayers(m_shownSet), m_shown->width, m_shown->height, m_shown->fullWidth(), m_shown->fullHeight());
    else m_viewer.setImage(m_shown);
    const ImagePtr imageB = compareImage(m_shownSet);
    m_viewer.setCompare(imageB, m_cmpMode, m_wipe, m_cmpSwap, m_diffGain, AutoAlphaMode(imageB));
    m_viewer.setCheck(m_check);
    updateScopes();
    countBadPixels();
    double viewerMs = (Now() - viewerStart) * 1000.0;
    // While playing, the next frame is uploaded in the background as this one shows
    // (from the second frame on: the uploader starts then, off the startup path).
    std::vector<ImagePtr> upcoming;
    if (m_playDir != 0 && !m_exporter && m_framesRendered > 0) {
        bool reverse = false;
        const int next = nextIndex(m_index, m_playDir, reverse);
        if (const FrameSetPtr set = next >= 0 ? m_cache.get(next) : nullptr) {
            if (!stackActive()) upcoming.push_back(set->images[0]);
            else for (const CompLayer& l : compLayers(set)) upcoming.push_back(l.image);
            if (const ImagePtr b = compareImage(set)) upcoming.push_back(b);
        }
    }
    m_viewer.prefetch(upcoming);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    handleShortcuts();
    drawUI();
    ImGui::Render();

    RECT rc;
    GetClientRect(m_hwnd, &rc);
    const int fbW = rc.right - rc.left, fbH = rc.bottom - rc.top;
    if (fbW > 0 && fbH > 0) {
        const int top = (int)m_topBarH, bottom = (int)m_bottomBarH;
        glViewport(0, 0, fbW, fbH);
        glClearColor(0.105f, 0.105f, 0.117f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        const int viewW = std::max(1, fbW - (int)m_panelW);
        const double drawStart = Now();
        m_viewer.draw(fbW, fbH, 0, bottom, viewW, std::max(1, fbH - top - bottom), m_zoom, m_panX, m_panY, m_channel);
        viewerMs += (Now() - drawStart) * 1000.0;
        m_viewerMsMax = std::max(m_viewerMsMax, viewerMs);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        // SP_DUMP=1: save the 4th frame (UI settled) for headless visual checks;
        // SP_DUMP=<ms>: the first frame after that time. GDI screen capture cannot
        // see GL windows on some drivers.
        ++m_framesRendered;
        if (const wchar_t* d = _wgetenv(L"SP_DUMP")) {
            static bool dumped = false;
            const int ms = _wtoi(d);
            if (!dumped && (ms > 1 ? ImGui::GetTime() * 1000.0 >= ms : m_framesRendered == 4)) {
                DumpBackbuffer(fbW, fbH);
                dumped = true;
            }
        }
        SwapBuffers(m_hdc);
    }
    m_inFrame = false;
}
