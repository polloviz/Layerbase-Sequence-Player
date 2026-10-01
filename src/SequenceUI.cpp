// The sequence on disk: live refresh while it renders, versions, frame report.
#include "App.h"
#include "I18n.h"
#include "ImageIO.h"
#include "Platform.h"

#include <windows.h>
#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <cstdio>
#include <mutex>
#include <thread>

// ---------------------------------------------------------------------------
// Live refresh

void App::startWatching()
{
    if (!m_seq || !m_settings.liveRefresh || m_batchMode) {
        m_watcher.stop();
        return;
    }
    if (ToLower(m_watcher.dir()) == ToLower(m_seq->directory)) return;
    m_watcher.start(m_seq->directory, [hwnd = m_hwnd] { PostMessageW(hwnd, kDirChangedMsg, 0, 0); });
}

void App::refreshSequence()
{
    if (m_exporter || m_batchRunning) return;   // stays pending until the export ends
    m_refreshFirst = m_refreshDue = 0.0;
    if (!m_seq) return;
    auto fresh = std::make_shared<Sequence>(RescanSequence(*m_seq));
    if (fresh->empty()) return;   // everything gone (folder cleaned up): keep what is shown
    const Sequence& old = *m_seq;

    // Old frame -> new frame, by number; rewritten files are decoded again.
    std::vector<int> from(old.frames.size(), -1);
    int kept = 0, rewritten = 0;
    size_t j = 0;
    for (size_t i = 0; i < old.frames.size(); ++i) {
        while (j < fresh->frames.size() && fresh->frames[j].number < old.frames[i].number) ++j;
        if (j == fresh->frames.size() || fresh->frames[j].number != old.frames[i].number) continue;
        const SequenceFrame &a = old.frames[i], &b = fresh->frames[j];
        if (a.stamp && (a.stamp != b.stamp || a.size != b.size)) {
            ++rewritten;
            continue;
        }
        from[i] = (int)j;
        ++kept;
    }
    if (kept == old.count() && fresh->count() == old.count()) return;   // another file of the folder changed
    const int added = fresh->count() - kept - rewritten;

    const int number = old.frames[m_index].number, inNumber = old.frames[m_in].number, outNumber = old.frames[m_out].number;
    const bool toEnd = m_out >= old.count() - 1, fromStart = m_in == 0;
    m_seq = fresh;
    m_cache.remap(m_seq, from);
    m_index = m_seq->indexOfNumber(number);
    m_in = fromStart ? 0 : m_seq->indexOfNumber(inNumber);
    m_out = toEnd ? m_seq->count() - 1 : std::max(m_in, m_seq->indexOfNumber(outNumber));

    // Sequences matched to this one follow it (AOV passes and mattes render along).
    bool replan = false;
    for (StackLayer& l : m_stack) {
        if (!l.seq) continue;
        auto seq = std::make_shared<const Sequence>(RescanSequence(*l.seq));
        if (!seq->empty()) l.seq = seq;
        auto files = std::make_shared<const std::vector<std::wstring>>(MatchFrames(*m_seq, *l.seq));
        l.missing = (int)std::count(files->begin(), files->end(), std::wstring());
        l.files = files;
        replan = true;
    }
    if (m_cmpSeq) {
        auto seq = std::make_shared<const Sequence>(RescanSequence(*m_cmpSeq));
        if (!seq->empty()) m_cmpSeq = seq;
        rematchCompare();
        replan = true;
    }
    if (m_matteSeq) {
        Sequence seq = RescanSequence(*m_matteSeq);
        if (!seq.empty()) *m_matteSeq = std::move(seq);
        auto files = std::make_shared<std::vector<std::wstring>>(MatchFrames(*m_seq, *m_matteSeq));
        m_matteMissing = (int)std::count(files->begin(), files->end(), std::wstring());
        m_matteFiles = files;
        applyLoadOptions();
    } else if (replan) {
        applyLoadPlan();
    }
    Log("refresh: %d frames (+%d, %d rewritten)", m_seq->count(), added, rewritten);
    std::string msg;
    if (added > 0) msg = "+" + std::to_string(added) + " " + tr(S::FramesAdded);
    if (rewritten > 0) msg += (msg.empty() ? "" : "  \xC2\xB7  ") + std::to_string(rewritten) + " " + tr(S::FramesUpdated);
    if (!msg.empty()) showToast(msg);
}

// ---------------------------------------------------------------------------
// Versions

void App::switchVersion(int delta)
{
    if (!m_seq || m_exporter || m_batchRunning) return;
    const std::vector<SequenceVersion> versions = FindVersions(*m_seq);
    const auto cur = std::find_if(versions.begin(), versions.end(), [](const SequenceVersion& v) { return v.current; });
    if (cur == versions.end() || versions.size() < 2) {
        showToast(tr(S::NoVersions));
        return;
    }
    const int i = int(cur - versions.begin()) + delta;
    if (i < 0 || i >= (int)versions.size()) {
        showToast(delta > 0 ? tr(S::NoNewerVersion) : tr(S::NoOlderVersion));
        return;
    }
    openVersion(versions[i]);
}

void App::openVersion(const SequenceVersion& v)
{
    if (!m_seq || m_exporter || m_batchRunning) return;
    const std::wstring oldToken = VersionToken(*m_seq);
    // What carries over to the other version
    const int number = m_seq->frames[m_index].number;
    const bool rangeSet = m_in > 0 || m_out < frameCount() - 1;
    const int inNumber = m_seq->frames[m_in].number, outNumber = m_seq->frames[m_out].number;
    const bool inputChosen = m_inputUserChosen;
    const std::string input = m_color.input;
    const std::vector<uint32_t> cryptoSel = m_cryptoSel;
    const MatteMode matte = m_matte;
    const int crypto = m_crypto, alpha = m_alphaOverride, playDir = m_playDir, stackSel = m_stackSel;
    const bool fit = m_fit;
    const float zoom = m_zoom, panX = m_panX, panY = m_panY;
    const std::vector<StackLayer> stack = m_stack;
    const std::shared_ptr<const Sequence> cmpSeq = m_cmpSeq;
    const std::wstring matteFirst = m_matteSeq ? m_matteSeq->frames[0].path : std::wstring();
    // A sequence tied to the shot takes the new version when it has one.
    auto versioned = [&](const std::wstring& path) {
        const std::wstring p = ReplaceVersion(path, oldToken, v.token);
        if (p == path) return path;
        const Sequence seq = DetectSequence(p);
        return !seq.empty() && FileExists(seq.frames[0].path) ? p : path;
    };

    openPath(v.path);
    if (!m_seq || ToLower(m_seq->frames[0].path) != ToLower(v.path)) return;   // could not be opened
    m_index = m_seq->indexOfNumber(number);
    if (rangeSet) {
        m_in = m_seq->indexOfNumber(inNumber);
        m_out = std::max(m_in, m_seq->indexOfNumber(outNumber));
    }
    if (inputChosen && m_color.hasColorSpace(input)) {
        m_color.input = input;
        m_inputUserChosen = true;
        m_colorDirty = true;
    }
    m_alphaOverride = alpha;
    if (!matteFirst.empty()) loadMatteSequence(versioned(matteFirst));
    if (hasCrypto() && matte != MatteMode::Off && stack.empty()) {
        m_crypto = std::clamp(crypto, 0, (int)cryptoLayers().size() - 1);
        m_cryptoSel = cryptoSel;
        m_matte = matte;
        applyLoadOptions();
    }
    if (!stack.empty()) {
        if (m_matte != MatteMode::Off) setMatteMode(MatteMode::Off);
        m_cryptoPanel = false;
        const auto ownLayers = m_exrInfo.layers.empty() ? nullptr : std::make_shared<const std::vector<ExrLayer>>(m_exrInfo.layers);
        for (const StackLayer& old : stack) {
            StackLayer l;
            if (!old.seq) {
                l.exrLayers = ownLayers;
                l.exrLayer = ownLayers ? m_exrInfo.defaultLayer : -1;
                l.isFloat = IsFloatFormat(m_seqExt);
                l.input = autoInput(l.isFloat, m_seq->frames[0].path);
            } else if (!makeSequenceLayer(versioned(old.seq->frames[0].path), l)) {
                continue;
            }
            const ExrLayer* was = old.exrLayers && old.exrLayer >= 0 && old.exrLayer < (int)old.exrLayers->size() ? &(*old.exrLayers)[old.exrLayer] : nullptr;
            if (was && l.exrLayers)
                for (size_t i = 0; i < l.exrLayers->size(); ++i)
                    if ((*l.exrLayers)[i].label == was->label) l.exrLayer = (int)i;
            l.blend = old.blend;
            l.opacity = old.opacity;
            l.exposure = old.exposure;
            l.visible = old.visible;
            if (!old.inputAuto && m_color.hasColorSpace(old.input)) {
                l.input = old.input;
                l.inputAuto = false;
            }
            stackPush(std::move(l));
        }
        m_stackSel = std::clamp(stackSel, 0, std::max(0, (int)m_stack.size() - 1));
    }
    if (cmpSeq && stack.empty()) setCompare(cmpSeq->frames[0].path);
    m_fit = fit;
    m_zoom = zoom;
    m_panX = panX;
    m_panY = panY;
    if (playDir) setPlaying(playDir);
    showToast(ToUtf8(v.token) + "  \xC2\xB7  " + ToUtf8(m_seq->displayName()));
}

void App::drawVersionCombo()
{
    const float s = m_dpiScale;
    const std::string label = ToUtf8(VersionToken(*m_seq));
    const ImGuiStyle& st = ImGui::GetStyle();
    ImGui::SetNextItemWidth(ImGui::CalcTextSize(label.c_str()).x + ImGui::GetFrameHeight() + st.FramePadding.x * 2);
    ImGui::SetNextWindowSizeConstraints(ImVec2(300 * s, 0), ImVec2(FLT_MAX, 420 * s));
    if (ImGui::BeginCombo("##version", label.c_str(), ImGuiComboFlags_HeightLarge)) {
        if (ImGui::IsWindowAppearing()) m_versions = FindVersions(*m_seq);
        if (m_versions.size() < 2) ImGui::TextDisabled("%s", tr(S::NoVersions));
        const SequenceVersion* pick = nullptr;
        for (auto it = m_versions.rbegin(); it != m_versions.rend(); ++it) {   // newest first
            char item[160];
            snprintf(item, sizeof(item), "%-8s  %d %s", ToUtf8(it->token).c_str(), it->frames, tr(S::Frames));
            ImGui::PushID(it->number);
            if (ImGui::Selectable(item, it->current) && !it->current) pick = &*it;
            ImGui::SetItemTooltip("%s", ToUtf8(it->path).c_str());
            ImGui::PopID();
        }
        if (pick) {
            const SequenceVersion v = *pick;
            defer([this, v] { openVersion(v); });
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("%s", tr(S::VersionHint));
}

// ---------------------------------------------------------------------------
// Frame report

struct App::FrameCheck {
    std::shared_ptr<const Sequence> seq;
    std::atomic<int> next{ 0 }, done{ 0 };
    std::atomic<bool> cancel{ false };
    std::mutex mutex;
    std::vector<std::pair<int, std::string>> errors;   // frame index, message
    std::vector<std::thread> threads;

    ~FrameCheck()
    {
        cancel = true;
        for (auto& t : threads)
            if (t.joinable()) t.join();
    }
};

void App::startFrameCheck()
{
    if (!m_seq) return;
    m_frameCheck.reset();   // stops a previous check
    auto check = std::make_shared<FrameCheck>();
    check->seq = m_seq;
    const unsigned workers = std::clamp(std::thread::hardware_concurrency() / 4, 1u, 4u);
    for (unsigned t = 0; t < workers; ++t)
        check->threads.emplace_back([c = check.get()] {
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
            for (int i = c->next++; !c->cancel && i < c->seq->count(); i = c->next++) {
                const ImagePtr img = LoadImageFile(c->seq->frames[i].path);
                if (!img || !img->valid()) {
                    std::lock_guard lock(c->mutex);
                    c->errors.push_back({ i, img ? img->error : std::string("?") });
                }
                ++c->done;
            }
        });
    m_frameCheck = check;
}

namespace {

struct Report {
    int first = 0, last = 0, count = 0;
    std::vector<std::pair<int, int>> gaps;              // missing numbers, inclusive ranges
    int missing = 0;
    std::vector<std::pair<int, const char*>> suspicious; // frame index, why
    std::vector<std::pair<int, std::string>> errors;     // frame index, message (sorted)
    bool checking = false, checked = false;
    float progress = 0.0f;
};

}  // namespace

static Report BuildReport(const Sequence& seq, const std::vector<std::pair<int, std::string>>& errors)
{
    Report r;
    r.count = seq.count();
    if (!r.count) return r;
    r.first = seq.frames.front().number;
    r.last = seq.frames.back().number;
    for (int i = 1; i < r.count; ++i) {
        const int a = seq.frames[i - 1].number, b = seq.frames[i].number;
        if (b - a > 1) {
            r.gaps.push_back({ a + 1, b - 1 });
            r.missing += b - a - 1;
        }
    }
    std::vector<uint64_t> sizes;
    for (const auto& f : seq.frames) sizes.push_back(f.size);
    std::nth_element(sizes.begin(), sizes.begin() + sizes.size() / 2, sizes.end());
    const uint64_t median = sizes[sizes.size() / 2];
    for (int i = 0; i < r.count; ++i) {
        const uint64_t size = seq.frames[i].size;
        if (size == 0) r.suspicious.push_back({ i, tr(S::EmptyFile) });
        else if (median > 64 * 1024 && size < median / 4) r.suspicious.push_back({ i, tr(S::SmallFile) });
    }
    r.errors = errors;
    std::sort(r.errors.begin(), r.errors.end());
    r.errors.erase(std::unique(r.errors.begin(), r.errors.end(), [](auto& a, auto& b) { return a.first == b.first; }), r.errors.end());
    return r;
}

std::string App::frameReportText() const
{
    if (!m_seq) return {};
    std::vector<std::pair<int, std::string>> errors;
    for (int i = 0; i < (int)m_cacheMask.size(); ++i)
        if (m_cacheMask[i] == 2)
            if (const FrameSetPtr set = m_cache.get(i))
                for (const ImagePtr& img : set->images)
                    if (img && !img->valid()) { errors.push_back({ i, img->error }); break; }
    if (m_frameCheck && m_frameCheck->seq == m_seq) {
        std::lock_guard lock(m_frameCheck->mutex);
        errors.insert(errors.end(), m_frameCheck->errors.begin(), m_frameCheck->errors.end());
    }
    const Report r = BuildReport(*m_seq, errors);
    std::string t = ToUtf8(m_seq->directory + L"\\" + m_seq->displayName()) + "\n";
    t += std::to_string(r.first) + "-" + std::to_string(r.last) + ", " + std::to_string(r.count) + " " + tr(S::Frames) + ", " +
         std::to_string(r.last - r.first + 1) + " " + tr(S::Expected) + "\n\n";
    t += std::string(tr(S::ReportMissing)) + ": " + std::to_string(r.missing) + "\n";
    for (const auto& [a, b] : r.gaps) t += "  " + (a == b ? std::to_string(a) : std::to_string(a) + "-" + std::to_string(b)) + "\n";
    if (!r.suspicious.empty()) {
        t += std::string("\n") + tr(S::ReportSmall) + ":\n";
        for (const auto& [i, why] : r.suspicious) t += "  " + std::to_string(m_seq->frames[i].number) + "  " + why + "\n";
    }
    if (!r.errors.empty()) {
        t += std::string("\n") + tr(S::ReportErrors) + ":\n";
        for (const auto& [i, msg] : r.errors) t += "  " + std::to_string(m_seq->frames[i].number) + "  " + msg + "\n";
    }
    return t;
}

void App::drawFrameReport()
{
    if (!m_reportOpen || !m_seq) return;
    const float s = m_dpiScale;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.45f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(460 * s, 440 * s), ImGuiCond_Appearing);
    const std::string title = std::string(tr(S::ReportTitle)) + "###report";
    if (!ImGui::Begin(title.c_str(), &m_reportOpen, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    std::vector<std::pair<int, std::string>> errors;
    for (int i = 0; i < (int)m_cacheMask.size(); ++i)
        if (m_cacheMask[i] == 2)
            if (const FrameSetPtr set = m_cache.get(i))
                for (const ImagePtr& img : set->images)
                    if (img && !img->valid()) { errors.push_back({ i, img->error }); break; }
    bool checking = false, checked = false;
    float progress = 0.0f;
    if (m_frameCheck && m_frameCheck->seq == m_seq) {
        std::lock_guard lock(m_frameCheck->mutex);
        errors.insert(errors.end(), m_frameCheck->errors.begin(), m_frameCheck->errors.end());
        const int done = m_frameCheck->done;
        checked = done >= m_seq->count();
        checking = !checked;
        progress = float(done) / std::max(1, m_seq->count());
    }
    const Report r = BuildReport(*m_seq, errors);

    ImGui::TextUnformatted(ToUtf8(m_seq->displayName()).c_str());
    ImGui::SetItemTooltip("%s", ToUtf8(m_seq->directory).c_str());
    ImGui::TextDisabled("%d \xE2\x80\x93 %d  \xC2\xB7  %d %s  \xC2\xB7  %d %s", r.first, r.last, r.count, tr(S::Frames), r.last - r.first + 1,
                        tr(S::Expected));
    ImGui::Spacing();

    const ImVec4 warn(1.0f, 0.62f, 0.3f, 1), bad(1.0f, 0.42f, 0.42f, 1), good(0.45f, 0.85f, 0.5f, 1);
    auto jump = [&](int index, const std::string& label) {
        ImGui::PushID(index);
        if (ImGui::Selectable(label.c_str())) {
            m_playDir = 0;
            seek(index);
        }
        ImGui::PopID();
    };
    ImGui::BeginChild("##reportlist", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 3.2f), ImGuiChildFlags_Borders);
    if (r.gaps.empty()) {
        ImGui::TextColored(good, "%s", tr(S::ReportNoMissing));
    } else {
        ImGui::TextColored(warn, "%s: %d", tr(S::ReportMissing), r.missing);
        for (const auto& [a, b] : r.gaps) {
            char buf[64];
            if (a == b) snprintf(buf, sizeof(buf), "   %d", a);
            else snprintf(buf, sizeof(buf), "   %d \xE2\x80\x93 %d  (%d)", a, b, b - a + 1);
            jump(m_seq->indexOfNumber(a), buf);
        }
    }
    if (!r.suspicious.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(warn, "%s", tr(S::ReportSmall));
        for (const auto& [i, why] : r.suspicious) jump(i, "   " + std::to_string(m_seq->frames[i].number) + "   " + why);
    }
    ImGui::Spacing();
    if (!r.errors.empty()) {
        ImGui::TextColored(bad, "%s: %d", tr(S::ReportErrors), (int)r.errors.size());
        for (const auto& [i, msg] : r.errors) {
            jump(i, "   " + std::to_string(m_seq->frames[i].number) + "   " + msg);
            ImGui::SetItemTooltip("%s", ToUtf8(m_seq->frames[i].path).c_str());
        }
    } else if (checked) {
        ImGui::TextColored(good, "%s", tr(S::ReportAllOk));
    } else {
        ImGui::TextDisabled("%s", tr(S::ReportNoErrors));
    }
    ImGui::EndChild();

    if (checking) {
        char overlay[64];
        snprintf(overlay, sizeof(overlay), "%s  %d / %d", tr(S::ReportChecking), m_frameCheck->done.load(), m_seq->count());
        ImGui::ProgressBar(progress, ImVec2(-FLT_MIN, 0), overlay);
        if (ImGui::Button(tr(S::Cancel))) m_frameCheck.reset();
    } else {
        if (ImGui::Button(tr(S::ReportCheckAll))) startFrameCheck();
        ImGui::SetItemTooltip("%s", tr(S::ReportCheckHint));
    }
    ImGui::SameLine();
    if (ImGui::Button(tr(S::CopyReport)) && SetClipboardText(m_hwnd, frameReportText())) showToast(tr(S::CopiedText));
    ImGui::End();
    if (checking) m_renderFrames = std::max(m_renderFrames, 2);   // the progress keeps moving
}
