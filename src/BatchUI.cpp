// Batch conversion: collect sequences (files, folders, subfolders) and export
// them one after another with the same movie settings and color pipeline.
#include "App.h"
#include "I18n.h"
#include "ImageIO.h"
#include "Platform.h"

#include <windows.h>
#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cstdio>

static double BatchSeconds()
{
    static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return double(t.QuadPart) / double(freq.QuadPart);
}

static std::wstring SanitizeForFileName(std::string s)
{
    for (auto& c : s)
        if (c == '.' || c == '/' || c == '\\' || c == ':' || c == ' ') c = '_';
    return FromUtf8(s);
}

void App::openBatchDialog()
{
    if (m_exporter || m_batchRunning) return;
    m_playDir = 0;
    m_ffmpeg = FindFFmpeg(FromUtf8(m_settings.ffmpegPath));
    m_batchOpt = ExportOptions();
    m_batchOpt.codec = (ExportCodec)m_settings.exportCodec;
    m_batchOpt.quality = (ExportQuality)m_settings.exportQuality;
    m_batchOpt.scalePercent = m_settings.exportScale;
    m_batchOpt.hardware = m_settings.exportHardware;
    m_batchOpt.fps = m_fps;
    m_batchLayer = (hasLayers() && m_layer != m_exrInfo.defaultLayer) ? currentLayerLabel() : std::string();
    m_batchInput = m_color.input;
    m_openBatch = true;
}

void App::batchAddPaths(const std::vector<std::wstring>& paths)
{
    auto add = [&](const Sequence& seq) {
        if (seq.empty()) return;
        const std::wstring key = ToLower(seq.frames[0].path);
        for (auto& it : m_batch)
            if (ToLower(it.path) == key) return;
        BatchItem it;
        it.path = seq.frames[0].path;
        it.name = seq.displayName();
        it.dir = seq.directory;
        it.base = SequenceBaseName(seq);
        it.frames = seq.count();
        it.first = seq.frames.front().number;
        it.last = seq.frames.back().number;
        m_batch.push_back(std::move(it));
    };
    for (const auto& p : paths) {
        if (IsDirectory(p)) {
            for (const auto& seq : FindSequences(p, m_settings.batchRecursive)) add(seq);
        } else if (IsSupportedExtension(GetFileExtension(p))) {
            add(DetectSequence(p));
        }
    }
}

void App::startBatch()
{
    for (auto& it : m_batch) {
        if (it.include) it.state = BatchItem::State::Pending;
        it.message.clear();
    }
    m_batchOutputs.clear();
    m_batchCurrent = -1;
    m_batchRunning = true;
    m_batchWaitingFrame = m_batchAwaitingResult = false;
    m_batchStartTime = BatchSeconds();
    m_settings.exportCodec = (int)m_batchOpt.codec;
    m_settings.exportQuality = (int)m_batchOpt.quality;
    m_settings.exportScale = m_batchOpt.scalePercent;
    m_settings.exportHardware = m_batchOpt.hardware;
    Log("batch started: %d sequences", (int)m_batch.size());
}

void App::cancelBatch()
{
    for (auto& it : m_batch)
        if (it.state == BatchItem::State::Pending || it.state == BatchItem::State::Running) it.state = BatchItem::State::Cancelled;
    m_batchRunning = m_batchWaitingFrame = m_batchAwaitingResult = false;
    m_openBatch = true;
}

// Called every frame: drives the batch one sequence at a time.
void App::processBatch()
{
    if (!m_batchRunning || m_exporter) return;

    if (m_batchAwaitingResult) {
        BatchItem& it = m_batch[m_batchCurrent];
        it.state = m_exportResultOk ? BatchItem::State::Done : BatchItem::State::Failed;
        it.message = m_exportResultMsg;
        m_batchAwaitingResult = false;
    }

    if (m_batchWaitingFrame) {
        if (!m_shown) return;   // first frame still decoding
        m_batchWaitingFrame = false;
        BatchItem& it = m_batch[m_batchCurrent];
        if (!m_shown->valid()) {
            it.state = BatchItem::State::Failed;
            it.message = m_shown->error;
            return;
        }
        m_exportOpt = m_batchOpt;
        m_exportOpt.width = m_shown->fullWidth();   // exports decode at full resolution
        m_exportOpt.height = m_shown->fullHeight();
        m_exportOpt.outputPath = it.output;
        m_exportInOut = false;
        m_exportResultReady = false;
        startExport();
        if (!m_exporter) {
            it.state = BatchItem::State::Failed;
            it.message = "FFmpeg";
            return;
        }
        m_batchAwaitingResult = true;
        return;
    }

    // Next sequence.
    int next = m_batchCurrent + 1;
    while (next < (int)m_batch.size() && !(m_batch[next].include && m_batch[next].state == BatchItem::State::Pending)) ++next;
    if (next >= (int)m_batch.size()) {
        m_batchRunning = false;
        int done = 0, failed = 0, skipped = 0;
        for (auto& it : m_batch) {
            done += it.state == BatchItem::State::Done;
            failed += it.state == BatchItem::State::Failed;
            skipped += it.state == BatchItem::State::Skipped;
        }
        char buf[160];
        snprintf(buf, sizeof(buf), "%s: %d %s, %d %s, %d %s", tr(S::BatchFinished), done, tr(S::StateDone), failed,
                 tr(S::StateFailed), skipped, tr(S::StateSkipped));
        showToast(buf, failed > 0);
        Log("batch finished in %.1f s: %d done, %d failed, %d skipped", BatchSeconds() - m_batchStartTime, done, failed, skipped);
        if (m_quitAfterBatch) {
            if (failed) m_exitCode = 1;
            PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
            return;
        }
        m_openBatch = true;
        return;
    }

    m_batchCurrent = next;
    BatchItem& it = m_batch[next];

    // Output path: next to the sequence or in the chosen folder, never reusing a name within the batch.
    const std::wstring dir = m_settings.batchDest == 1 && !m_settings.batchFolder.empty() ? FromUtf8(m_settings.batchFolder) : it.dir;
    std::wstring base = it.base;
    if (!m_batchLayer.empty() && m_settings.batchUseLayer) base += L"_" + SanitizeForFileName(m_batchLayer);
    const std::wstring ext = ExportCodecExtension(m_batchOpt.codec);
    auto taken = [&](const std::wstring& p) {
        const std::wstring l = ToLower(p);
        return std::any_of(m_batchOutputs.begin(), m_batchOutputs.end(), [&](auto& o) { return ToLower(o) == l; });
    };
    std::wstring out = dir + L"\\" + base + ext;
    if (taken(out)) out = dir + L"\\" + base + L"_" + GetFileName(it.dir) + ext;
    for (int n = 2; taken(out); ++n) out = dir + L"\\" + base + L"_" + std::to_wstring(n) + ext;
    m_batchOutputs.push_back(out);
    it.output = out;

    if (m_settings.batchSkipExisting && FileExists(out)) {
        it.state = BatchItem::State::Skipped;
        return;
    }
    it.state = BatchItem::State::Running;
    openPath(it.path, false);
    if (!m_batchLayer.empty() && m_settings.batchUseLayer)
        for (size_t i = 0; i < m_exrInfo.layers.size(); ++i)
            if (m_exrInfo.layers[i].label == m_batchLayer) setLayer((int)i);
    if (m_settings.batchCurrentInput && m_color.hasColorSpace(m_batchInput)) {
        m_color.input = m_batchInput;
        m_colorDirty = true;
    }
    m_batchWaitingFrame = true;
}

void App::drawBatchDialog()
{
    const float s = m_dpiScale;
    if (m_openBatch) {
        ImGui::OpenPopup("###batch");
        m_openBatch = false;
    }
    m_batchDialogOpen = false;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(860 * s, vp->Size.x - 40 * s), 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18 * s, 14 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10 * s);
    const std::string title = std::string(tr(S::BatchTitle)) + "###batch";
    if (ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
        m_batchDialogOpen = true;
        const ImGuiStyle& st = ImGui::GetStyle();

        // Toolbar
        if (ImGui::Button(tr(S::AddSequences))) {
            defer([this] {
                batchAddPaths(ShowOpenImagesDialog(m_hwnd, FromUtf8(tr(S::AddSequences)).c_str()));
                m_openBatch = true;
            });
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr(S::AddFolder))) {
            defer([this] {
                const std::wstring dir = ShowOpenFileDialog(m_hwnd, true, FromUtf8(tr(S::AddFolder)).c_str());
                if (!dir.empty()) batchAddPaths({ dir });
                m_openBatch = true;
            });
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!m_seq);
        if (ImGui::Button(tr(S::AddCurrent)) && m_seq) batchAddPaths({ m_seq->frames[0].path });
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::Checkbox(tr(S::IncludeSubfolders), &m_settings.batchRecursive);
        {
            const float w1 = ImGui::CalcTextSize(tr(S::RemoveUnchecked)).x + st.FramePadding.x * 2;
            const float w2 = ImGui::CalcTextSize(tr(S::ClearList)).x + st.FramePadding.x * 2;
            ImGui::SameLine(ImGui::GetWindowWidth() - st.WindowPadding.x - w1 - w2 - st.ItemSpacing.x);
            if (ImGui::Button(tr(S::RemoveUnchecked)))
                m_batch.erase(std::remove_if(m_batch.begin(), m_batch.end(), [](auto& it) { return !it.include; }), m_batch.end());
            ImGui::SameLine();
            if (ImGui::Button(tr(S::ClearList))) m_batch.clear();
        }

        // Sequence list
        const ImGuiTableFlags tf = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY |
                                   ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable;
        if (ImGui::BeginTable("##seqs", 5, tf, ImVec2(0, 250 * s))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
            ImGui::TableSetupColumn(tr(S::Sequence), ImGuiTableColumnFlags_WidthStretch, 3.0f);
            ImGui::TableSetupColumn(tr(S::FramesHeader), ImGuiTableColumnFlags_WidthStretch, 1.2f);
            ImGui::TableSetupColumn(tr(S::Folder), ImGuiTableColumnFlags_WidthStretch, 3.0f);
            ImGui::TableSetupColumn(tr(S::Status), ImGuiTableColumnFlags_WidthStretch, 1.6f);
            ImGui::TableHeadersRow();
            for (int i = 0; i < (int)m_batch.size(); ++i) {
                BatchItem& it = m_batch[i];
                ImGui::PushID(i);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Checkbox("##inc", &it.include);
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(ToUtf8(it.name).c_str());
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%d  (%d\xE2\x80\x93%d)", it.frames, it.first, it.last);
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", ToUtf8(it.dir).c_str());
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", ToUtf8(it.dir).c_str());
                ImGui::TableNextColumn();
                using St = BatchItem::State;
                const char* label = it.state == St::Pending ? tr(S::StatePending) : it.state == St::Running ? tr(S::StateRunning)
                                  : it.state == St::Done ? tr(S::StateDone) : it.state == St::Failed ? tr(S::StateFailed)
                                  : it.state == St::Skipped ? tr(S::StateSkipped) : tr(S::StateCancelled);
                const ImVec4 col = it.state == St::Done ? ImVec4(0.45f, 0.85f, 0.5f, 1) : it.state == St::Failed ? ImVec4(1, 0.45f, 0.4f, 1)
                                 : ImVec4(0.6f, 0.6f, 0.65f, 1);
                ImGui::TextColored(col, "%s", label);
                if (!it.message.empty() || !it.output.empty())
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%s", ToUtf8(it.output).c_str(), it.message.c_str());
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (m_batch.empty()) {
            const ImVec2 ts = ImGui::CalcTextSize(tr(S::BatchEmpty));
            const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddText(ImVec2((min.x + max.x - ts.x) * 0.5f, (min.y + max.y - ts.y) * 0.5f),
                                                ImGui::GetColorU32(ImGuiCol_TextDisabled), tr(S::BatchEmpty));
        }

        // Settings
        ImGui::Spacing();
        const float labelW = 170 * s;
        auto row = [&](const char* label) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            ImGui::SameLine(labelW);
            ImGui::SetNextItemWidth(-FLT_MIN);
        };
        drawCodecRows(m_batchOpt, labelW);

        row(tr(S::FrameRate));
        double fps = m_batchOpt.fps;
        if (ImGui::InputDouble("##bfps", &fps, 0, 0, "%.3f")) m_batchOpt.fps = std::clamp(fps, 1.0, 240.0);

        if (!m_batchLayer.empty()) {
            row(tr(S::Layer));
            const std::string l = std::string(tr(S::UseLayer)) + ": " + m_batchLayer;
            ImGui::Checkbox(l.c_str(), &m_settings.batchUseLayer);
        }

        row(tr(S::InputColor));
        if (ImGui::RadioButton(tr(S::InputAuto), !m_settings.batchCurrentInput)) m_settings.batchCurrentInput = false;
        ImGui::SameLine();
        const std::string cur = std::string(tr(S::InputCurrent)) + ": " + m_batchInput;
        if (ImGui::RadioButton(cur.c_str(), m_settings.batchCurrentInput)) m_settings.batchCurrentInput = true;

        row(tr(S::ColorBaked));
        if (m_colorManaged)
            ImGui::TextDisabled("%s / %s  \xC2\xB7  %s", m_color.display.c_str(), m_color.view.c_str(), m_color.sourceLabel().c_str());
        else
            ImGui::TextDisabled("%s", tr(S::ColorOff));

        row(tr(S::Destination));
        if (ImGui::RadioButton(tr(S::NextToSequence), m_settings.batchDest == 0)) m_settings.batchDest = 0;
        ImGui::SameLine();
        if (ImGui::RadioButton(tr(S::ToFolder), m_settings.batchDest == 1)) m_settings.batchDest = 1;
        if (m_settings.batchDest == 1) {
            ImGui::SameLine();
            const float bw = ImGui::CalcTextSize(tr(S::Browse)).x + st.FramePadding.x * 2;
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - bw - st.ItemSpacing.x);
            char buf[1024];
            snprintf(buf, sizeof(buf), "%s", m_settings.batchFolder.c_str());
            if (ImGui::InputText("##bdir", buf, sizeof(buf))) m_settings.batchFolder = buf;
            ImGui::SameLine();
            if (ImGui::Button(tr(S::Browse))) {
                defer([this] {
                    const std::wstring dir = ShowOpenFileDialog(m_hwnd, true, FromUtf8(tr(S::Destination)).c_str());
                    if (!dir.empty()) m_settings.batchFolder = ToUtf8(dir);
                    m_openBatch = true;
                });
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SetCursorPosX(labelW);
        ImGui::Checkbox(tr(S::SkipExisting), &m_settings.batchSkipExisting);

        if (!m_ffmpeg.found()) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1, 0.55f, 0.4f, 1), "%s", tr(S::FFmpegMissing));
        }

        // Buttons
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        int count = 0;
        for (auto& it : m_batch) count += it.include;
        const float bw = 150 * s;
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 2 * bw - st.ItemSpacing.x - st.WindowPadding.x);
        if (ImGui::Button(tr(S::Close), ImVec2(bw, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            m_settings.save();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        const bool folderOk = m_settings.batchDest == 0 || IsDirectory(FromUtf8(m_settings.batchFolder));
        ImGui::BeginDisabled(count == 0 || !m_ffmpeg.found() || !folderOk);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.29f, 0.56f, 1.0f, 0.75f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.29f, 0.56f, 1.0f, 0.9f));
        char start[64];
        snprintf(start, sizeof(start), "%s (%d)", tr(S::StartBatch), count);
        if (ImGui::Button(start, ImVec2(bw, 0))) {
            startBatch();
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor(2);
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);
}
