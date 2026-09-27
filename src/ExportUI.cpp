// Movie export: dialog, progress and the frame pump (cache -> GPU color -> FFmpeg).
#include "App.h"
#include "I18n.h"
#include "Platform.h"

#include <windows.h>
#include <shellapi.h>
#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>

static double Seconds()
{
    static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return double(t.QuadPart) / double(freq.QuadPart);
}

static std::string LowerAscii(std::string s)
{
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

static std::string FormatDuration(double sec)
{
    const int s = (int)std::max(0.0, sec);
    char buf[32];
    if (s >= 3600) snprintf(buf, sizeof(buf), "%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60);
    else snprintf(buf, sizeof(buf), "%d:%02d", s / 60, s % 60);
    return buf;
}

void App::deriveColorTags(ExportOptions& opt) const
{
    // Tag the file with what the pixels are: the selected OCIO display encoding.
    const std::string d = m_colorManaged ? LowerAscii(m_color.display) : "srgb";
    opt.primaries = "bt709";
    opt.transfer = "iec61966-2-1";
    opt.matrix = "bt709";
    if (d.find("pq") != std::string::npos || d.find("2100") != std::string::npos || d.find("st2084") != std::string::npos) {
        opt.primaries = "bt2020"; opt.transfer = "smpte2084"; opt.matrix = "bt2020nc";
    } else if (d.find("hlg") != std::string::npos) {
        opt.primaries = "bt2020"; opt.transfer = "arib-std-b67"; opt.matrix = "bt2020nc";
    } else if (d.find("2020") != std::string::npos) {
        opt.primaries = "bt2020"; opt.transfer = "bt709"; opt.matrix = "bt2020nc";
    } else if (d.find("p3") != std::string::npos) {
        opt.primaries = "smpte432";
    } else if (d.find("srgb") == std::string::npos &&
               (d.find("1886") != std::string::npos || d.find("709") != std::string::npos)) {
        opt.transfer = "bt709";
    }
}

std::wstring App::defaultExportPath() const
{
    if (!m_seq) return {};
    std::wstring base = m_seq->prefix;
    while (!base.empty() && (base.back() == L'.' || base.back() == L'_' || base.back() == L'-' || base.back() == L' ')) base.pop_back();
    if (base.empty()) {
        base = GetFileName(m_seq->frames[0].path);
        const size_t dot = base.find_last_of(L'.');
        if (dot != std::wstring::npos) base = base.substr(0, dot);
    }
    return m_seq->directory + L"\\" + base + ExportCodecExtension(m_exportOpt.codec);
}

void App::openExportDialog()
{
    if (!m_seq || !m_shown || !m_shown->valid() || m_exporter) return;
    m_playDir = 0;
    m_exportOpt.codec = (ExportCodec)m_settings.exportCodec;
    m_exportOpt.quality = (ExportQuality)m_settings.exportQuality;
    m_exportOpt.scalePercent = m_settings.exportScale;
    m_exportOpt.hardware = m_settings.exportHardware;
    m_exportOpt.fps = m_fps;
    m_exportOpt.width = m_shown->fullWidth();   // exports decode at full resolution
    m_exportOpt.height = m_shown->fullHeight();
    m_exportOpt.outputPath = defaultExportPath();
    m_exportInOut = m_in > 0 || m_out < frameCount() - 1;
    m_ffmpeg = FindFFmpeg(FromUtf8(m_settings.ffmpegPath));
    m_openExport = true;
}

void App::startExport()
{
    deriveColorTags(m_exportOpt);
    m_settings.exportCodec = (int)m_exportOpt.codec;
    m_settings.exportQuality = (int)m_exportOpt.quality;
    m_settings.exportScale = m_exportOpt.scalePercent;
    m_settings.exportHardware = m_exportOpt.hardware;

    auto exporter = std::make_unique<MovieExporter>();
    std::string err;
    if (!exporter->start(m_ffmpeg, m_exportOpt, err)) {
        showToast(std::string(tr(S::ExportFailed)) + ": " + err, true);
        return;
    }
    m_exporter = std::move(exporter);
    m_exportFirst = m_exportInOut ? m_in : 0;
    m_exportLast = m_exportInOut ? m_out : frameCount() - 1;
    m_exportNext = m_exportFirst;
    m_exportFinishing = false;
    m_exportStartTime = Seconds();
    m_playDir = 0;
    m_index = m_exportFirst;
    if (m_planProxy != 1) applyLoadPlan();   // exports read full resolution
    Log("export started: %s", ToUtf8(m_exportOpt.outputPath).c_str());
}

// Called every frame while exporting. Pushes as many frames as fit in ~30 ms
// so the UI stays responsive; decoding runs ahead in the frame cache.
void App::processExport()
{
    // Command-line export starts once the first frame is decoded.
    if (!m_cliExport.exportPath.empty() && !m_exporter && m_shown && m_shown->valid()) {
        static const struct { const char* name; ExportCodec codec; } codecs[] = {
            { "h264", ExportCodec::H264 }, { "h265", ExportCodec::H265 }, { "hevc", ExportCodec::H265 },
            { "prores-proxy", ExportCodec::ProRes422Proxy }, { "prores-lt", ExportCodec::ProRes422LT },
            { "prores", ExportCodec::ProRes422 }, { "prores-hq", ExportCodec::ProRes422HQ }, { "prores-4444", ExportCodec::ProRes4444 },
        };
        openExportDialog();
        m_openExport = false;
        m_exportOpt.outputPath = m_cliExport.exportPath;
        m_exportOpt.hardware = m_cliExport.exportHardware;
        m_exportOpt.alpha = m_cliExport.exportAlpha;
        for (auto& c : codecs)
            if (m_cliExport.exportCodec == c.name) m_exportOpt.codec = c.codec;
        if (m_cliExport.exportCodec.empty()) {
            const std::wstring ext = GetFileExtension(m_exportOpt.outputPath);
            m_exportOpt.codec = ext == L".mov" ? ExportCodec::ProRes422HQ : ExportCodec::H264;
        }
        m_cliExport = StartupOptions();
        m_quitAfterExport = true;
        startExport();
        if (!m_exporter) m_exitCode = 1;
        return;
    }
    if (!m_exporter) {
        if (m_quitAfterExport) {          // command-line export ended (any path)
            m_quitAfterExport = false;
            PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
        }
        return;
    }
    // Ends the current export: records the result for the batch runner and the exit code.
    auto finishExport = [this](bool ok, const std::string& msg, bool cancelExporter) {
        if (cancelExporter) m_exporter->cancel();
        m_exporter.reset();
        m_exportResultReady = true;
        m_exportResultOk = ok;
        m_exportResultMsg = msg;
        if (!ok) m_exitCode = 1;
        Log("export %s: %s", ok ? "done" : "failed", msg.c_str());
        if (m_batchRunning) return;   // the batch dialog reports per item
        if (ok) showToast(std::string(tr(S::Exported)) + ": " + msg);
        else showToast(std::string(tr(S::ExportFailed)) + ": " + msg, true);
    };

    if (!m_exporter->running()) {
        if (m_exporter->succeeded()) {
            m_lastExport = m_exportOpt.outputPath;
            finishExport(true, ToUtf8(GetFileName(m_exportOpt.outputPath)) + "  (" + FormatDuration(Seconds() - m_exportStartTime) + ")", false);
        } else {
            finishExport(false, m_exporter->error(), false);
        }
        return;
    }
    if (m_exportFinishing) return;
    if (!m_exporter->error().empty()) {       // ffmpeg died: writer thread will wind down
        m_exporter->finish();
        m_exportFinishing = true;
        return;
    }

    const double t0 = Seconds();
    std::vector<uint8_t> buf;
    while (m_exportNext <= m_exportLast && Seconds() - t0 < 0.030 && m_exporter->canPush()) {
        const FrameSetPtr set = m_cache.get(m_exportNext);
        if (!set) break;                                   // still decoding
        const ImagePtr img = baseImage(set);
        if (!img || !img->valid() || img->width != m_exportOpt.width || img->height != m_exportOpt.height) {
            const std::string why = !img ? "no image" : img->valid() ? "frame size changes within the sequence" : img->error;
            finishExport(false, ToUtf8(GetFileName(m_seq->frames[m_exportNext].path)) + " - " + why, true);
            return;
        }
        bool rendered;
        if (stackActive()) {
            m_viewer.setComposite(compLayers(set), img->width, img->height, img->fullWidth(), img->fullHeight());
            rendered = m_viewer.renderCompositeToMemory(m_channel, m_exporter->is16Bit(), m_exporter->hasAlpha(), buf);
        } else {
            m_viewer.setMatteMode(m_loadOpts && m_loadOpts->cryptoActive() ? m_matte : MatteMode::Off);
            rendered = m_viewer.renderToMemory(img, m_channel, m_exporter->is16Bit(), m_exporter->hasAlpha(), buf);
        }
        if (!rendered) {
            finishExport(false, "GPU render", true);
            return;
        }
        m_exporter->push(std::move(buf));
        m_index = m_exportNext;                            // timeline + viewer follow the export
        ++m_exportNext;
    }
    if (m_exportNext > m_exportLast) {
        m_exporter->finish();
        m_exportFinishing = true;
    }
}

void App::drawCodecRows(ExportOptions& o, float labelW)
{
    auto row = [&](const char* label) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SameLine(labelW);
        ImGui::SetNextItemWidth(-FLT_MIN);
    };

    row(tr(S::Format));
    if (ImGui::BeginCombo("##codec", ExportCodecName(o.codec))) {
        for (int i = 0; i < (int)ExportCodec::Count; ++i) {
            const ExportCodec c = (ExportCodec)i;
            const bool available = ExportCodecIsProRes(c) ? m_ffmpeg.prores
                : c == ExportCodec::H264 ? (m_ffmpeg.libx264 || m_ffmpeg.nvencH264 || m_ffmpeg.mfH264)
                                         : (m_ffmpeg.libx265 || m_ffmpeg.nvencH265 || m_ffmpeg.mfH265);
            ImGui::BeginDisabled(m_ffmpeg.found() && !available);
            if (ImGui::Selectable(ExportCodecName(c), o.codec == c)) {
                // Keep the chosen file name, swap the extension.
                std::wstring p = o.outputPath;
                const size_t dot = p.find_last_of(L'.'), slash = p.find_last_of(L"\\/");
                if (!p.empty() && dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash)) p = p.substr(0, dot);
                o.codec = c;
                if (!p.empty()) o.outputPath = p + ExportCodecExtension(c);
            }
            ImGui::EndDisabled();
        }
        ImGui::EndCombo();
    }

    const bool prores = ExportCodecIsProRes(o.codec);
    const bool nvencAvailable = o.codec == ExportCodec::H264 ? m_ffmpeg.nvencH264 : m_ffmpeg.nvencH265;
    if (!prores) {
        row(tr(S::Encoder));
        const char* encoders[] = { tr(S::EncoderSoftware), tr(S::EncoderNvenc) };
        int e = o.hardware && nvencAvailable ? 1 : 0;
        ImGui::BeginDisabled(!nvencAvailable);
        if (ImGui::Combo("##encoder", &e, encoders, 2)) o.hardware = e == 1;
        ImGui::EndDisabled();

        row(tr(S::Quality));
        const char* qualities[] = { tr(S::QualityHigh), tr(S::QualityMedium), tr(S::QualityLow) };
        int q = (int)o.quality;
        if (ImGui::Combo("##quality", &q, qualities, 3)) o.quality = (ExportQuality)q;
    } else {
        row(tr(S::Quality));
        ImGui::TextDisabled("%s", tr(S::QualityProRes));
    }
    if (o.codec == ExportCodec::ProRes4444) {
        row("Alpha");
        ImGui::Checkbox(tr(S::ExportAlpha), &o.alpha);
    }

    row(tr(S::Size));
    const int scales[] = { 100, 50, 25 };
    char label[64];
    auto even = [](double v) { return std::max(2, (int)std::lround(v / 2.0) * 2); };
    auto format = [&](int sc) {
        if (o.width > 0)
            snprintf(label, sizeof(label), "%d%%  (%d\xC3\x97%d)", sc, even(o.width * sc / 100.0), even(o.height * sc / 100.0));
        else
            snprintf(label, sizeof(label), "%d%%", sc);
        return label;
    };
    if (ImGui::BeginCombo("##scale", format(o.scalePercent))) {
        for (int sc : scales)
            if (ImGui::Selectable(format(sc), o.scalePercent == sc)) o.scalePercent = sc;
        ImGui::EndCombo();
    }
}

void App::drawExportDialog()
{
    const float s = m_dpiScale;
    if (m_openExport) {
        ImGui::OpenPopup("###export");
        m_openExport = false;
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(600 * s, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18 * s, 14 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10 * s);
    const std::string title = std::string(tr(S::ExportTitle)) + "###export";
    if (ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
        const float labelW = 130 * s;
        auto row = [&](const char* label) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            ImGui::SameLine(labelW);
            ImGui::SetNextItemWidth(-FLT_MIN);
        };
        ExportOptions& o = m_exportOpt;

        drawCodecRows(o, labelW);

        // Range
        row(tr(S::Range));
        {
            const int n = frameCount();
            char all[96], inout[96];
            snprintf(all, sizeof(all), "%s  (%d\xE2\x80\x93%d, %d)", tr(S::AllFrames), m_seq->frames.front().number,
                     m_seq->frames.back().number, n);
            snprintf(inout, sizeof(inout), "In/Out  (%d\xE2\x80\x93%d, %d)", m_seq->frames[m_in].number,
                     m_seq->frames[m_out].number, m_out - m_in + 1);
            if (ImGui::RadioButton(all, !m_exportInOut)) m_exportInOut = false;
            ImGui::SameLine();
            ImGui::BeginDisabled(m_in == 0 && m_out == n - 1);
            if (ImGui::RadioButton(inout, m_exportInOut)) m_exportInOut = true;
            ImGui::EndDisabled();
        }


        row(tr(S::FrameRate));
        double fps = o.fps;
        if (ImGui::InputDouble("##exportfps", &fps, 0, 0, "%.3f")) o.fps = std::clamp(fps, 1.0, 240.0);

        row(tr(S::ColorBaked));
        if (m_colorManaged && stackActive())
            ImGui::TextWrapped("%s (%d) \xE2\x86\x92 %s / %s  \xC2\xB7  %s", tr(S::StackTitle), (int)m_stack.size(), m_color.display.c_str(),
                               m_color.view.c_str(), tr(S::ColorAsViewed));
        else if (m_colorManaged)
            ImGui::TextWrapped("%s \xE2\x86\x92 %s / %s  \xC2\xB7  %s", m_color.input.c_str(), m_color.display.c_str(),
                               m_color.view.c_str(), tr(S::ColorAsViewed));
        else
            ImGui::TextWrapped("%s", tr(S::ColorOff));

        // Output
        row(tr(S::OutputFile));
        {
            std::string path = ToUtf8(o.outputPath);
            const float bw = ImGui::CalcTextSize(tr(S::Browse)).x + ImGui::GetStyle().FramePadding.x * 2;
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - bw - ImGui::GetStyle().ItemSpacing.x);
            char buf[1024];
            snprintf(buf, sizeof(buf), "%s", path.c_str());
            if (ImGui::InputText("##out", buf, sizeof(buf))) o.outputPath = FromUtf8(buf);
            ImGui::SameLine();
            if (ImGui::Button(tr(S::Browse))) {
                defer([this] {
                    const bool pr = ExportCodecIsProRes(m_exportOpt.codec);
                    std::wstring p = ShowSaveFileDialog(m_hwnd, FromUtf8(tr(S::ExportTitle)).c_str(), m_exportOpt.outputPath,
                                                        pr ? L"QuickTime (*.mov)" : L"MPEG-4 (*.mp4)", pr ? L"*.mov" : L"*.mp4",
                                                        ExportCodecExtension(m_exportOpt.codec));
                    if (!p.empty()) m_exportOpt.outputPath = p;
                    m_openExport = true;   // reopen the dialog after the native one
                });
                ImGui::CloseCurrentPopup();
            }
        }

        // FFmpeg status
        ImGui::Spacing();
        if (m_ffmpeg.found()) {
            ImGui::TextDisabled("FFmpeg: %s", ToUtf8(m_ffmpeg.path).c_str());
        } else {
            ImGui::TextColored(ImVec4(1, 0.55f, 0.4f, 1), "%s", tr(S::FFmpegMissing));
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(tr(S::LocateFFmpeg))) {
            defer([this] {
                std::wstring p = ShowOpenFilteredDialog(m_hwnd, FromUtf8(tr(S::LocateFFmpeg)).c_str(), L"ffmpeg.exe", L"ffmpeg.exe");
                if (!p.empty()) {
                    FFmpegInfo info = FindFFmpeg(p);
                    if (info.found() && info.path == p) {
                        m_ffmpeg = info;
                        m_settings.ffmpegPath = ToUtf8(p);
                    }
                }
                m_openExport = true;
            });
            ImGui::CloseCurrentPopup();
        }

        // Buttons
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        const float bw = 120 * s;
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 2 * bw - ImGui::GetStyle().ItemSpacing.x - ImGui::GetStyle().WindowPadding.x);
        if (ImGui::Button(tr(S::Cancel), ImVec2(bw, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        ImGui::BeginDisabled(!m_ffmpeg.found() || o.outputPath.empty());
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.29f, 0.56f, 1.0f, 0.75f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.29f, 0.56f, 1.0f, 0.9f));
        if (ImGui::Button(tr(S::Export), ImVec2(bw, 0))) {
            ImGui::CloseCurrentPopup();
            startExport();
        }
        ImGui::PopStyleColor(2);
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);
}

void App::drawExportProgress()
{
    const float s = m_dpiScale;
    if (m_exporter && !ImGui::IsPopupOpen("###exporting")) ImGui::OpenPopup("###exporting");
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(480 * s, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18 * s, 14 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10 * s);
    const std::string title = std::string(tr(S::Exporting)) + "###exporting";
    if (ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                                                           ImGuiWindowFlags_NoMove)) {
        if (!m_exporter) {
            ImGui::CloseCurrentPopup();
        } else {
            const int total = m_exportLast - m_exportFirst + 1;
            const int done = m_exporter->framesWritten();
            const double elapsed = Seconds() - m_exportStartTime;
            const double fpsRate = done > 0 ? done / std::max(1e-3, elapsed) : 0.0;
            if (m_batchRunning) {
                int total = 0, pos = 0;
                for (int i = 0; i < (int)m_batch.size(); ++i)
                    if (m_batch[i].include) { ++total; if (i <= m_batchCurrent) ++pos; }
                ImGui::TextColored(ImVec4(0.55f, 0.72f, 1.0f, 1), "%s  %d / %d", tr(S::BatchTitle), pos, total);
                ImGui::ProgressBar(total > 0 ? float(pos - 1) / total : 0.0f, ImVec2(-FLT_MIN, 4 * s), "");
            }
            ImGui::TextUnformatted(ToUtf8(GetFileName(m_exportOpt.outputPath)).c_str());
            ImGui::TextDisabled("%s", ExportCodecName(m_exportOpt.codec));
            ImGui::Spacing();
            char overlay[64];
            snprintf(overlay, sizeof(overlay), "%d / %d", done, total);
            ImGui::ProgressBar(total > 0 ? float(done) / total : 0.0f, ImVec2(-FLT_MIN, 0), overlay);
            if (fpsRate > 0)
                ImGui::TextDisabled("%.1f fps  \xC2\xB7  %s  \xC2\xB7  %s %s", fpsRate, FormatDuration(elapsed).c_str(),
                                    FormatDuration((total - done) / fpsRate).c_str(), tr(S::Remaining));
            else
                ImGui::TextDisabled("%s", FormatDuration(elapsed).c_str());
            ImGui::Spacing();
            const float bw = 120 * s;
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - bw - ImGui::GetStyle().WindowPadding.x);
            if (ImGui::Button(tr(S::Cancel), ImVec2(bw, 0))) {
                if (m_batchRunning) cancelBatch();
                m_exporter->cancel();
                m_exporter.reset();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);
}
