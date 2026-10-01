// A/B compare: a second sequence (another version, another render) decoded with the open
// one, matched by frame number, shown as a wipe, side by side, difference or toggle.
#include "App.h"
#include "I18n.h"
#include "ImageIO.h"
#include "Platform.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cstdio>

std::string App::compareInput() const
{
    return m_cmpInput.empty() ? m_color.input : m_cmpInput;
}

ImagePtr App::compareImage(const FrameSetPtr& set) const
{
    return set && compareActive() ? set->find(m_cmpKey) : nullptr;
}

void App::resetCompare()
{
    m_cmpSeq.reset();
    m_cmpFiles.reset();
    m_cmpMissing = 0;
    m_cmpOpts.reset();
    m_cmpKey.clear();
    m_cmpInput.clear();
    m_wipeDrag = false;
    m_hoverB = false;
    m_viewer.releaseCompare();
}

void App::clearCompare()
{
    if (!compareActive()) return;
    resetCompare();
    applyLoadPlan();
}

void App::rematchCompare()
{
    if (!m_cmpSeq || !m_seq) return;
    auto files = std::make_shared<const std::vector<std::wstring>>(MatchFrames(*m_seq, *m_cmpSeq));
    m_cmpMissing = (int)std::count(files->begin(), files->end(), std::wstring());
    m_cmpFiles = files;
}

void App::setCompare(const std::wstring& path, bool announce)
{
    if (!m_seq || stackActive()) return;
    int start = 0;
    Sequence seq = DetectSequence(path, &start);
    if (seq.empty() || !IsSupportedExtension(GetFileExtension(seq.frames[0].path)) || !FileExists(seq.frames[start].path)) {
        showToast(std::string(tr(S::NotASequence)) + " " + ToUtf8(GetFileName(path)), true);
        return;
    }
    const std::wstring ext = GetFileExtension(seq.frames[0].path);
    // B shows the EXR layer shown for A when it has one with that name.
    auto opts = std::make_shared<LoadOptions>();
    std::string layer;
    if (ext == L".exr") {
        const ExrInfo info = ReadExrInfo(seq.frames[start].path);
        const std::string label = currentLayerLabel();
        for (const ExrLayer& l : info.layers)
            if (l.label == label && !l.isRootColor) {
                opts->part = l.part;
                opts->channels = l.channels;
                layer = "|" + label;
            }
    }
    const bool wasActive = compareActive();
    m_cmpSeq = std::make_shared<const Sequence>(std::move(seq));
    m_cmpOpts = opts;
    m_cmpKey = "cmp|" + ToUtf8(ToLower(m_cmpSeq->directory + L"\\" + m_cmpSeq->displayName())) + layer;
    m_cmpIsFloat = IsFloatFormat(ext);
    // The same kind of file as A (another version): B follows A's input space.
    m_cmpInput = m_cmpIsFloat == IsFloatFormat(m_seqExt) ? std::string() : autoInput(m_cmpIsFloat, m_cmpSeq->frames[0].path);
    if (!wasActive) m_wipe = 0.5f;
    rematchCompare();
    applyLoadPlan();
    m_colorDirty = true;
    if (!announce) return;
    std::string msg = std::string(tr(S::Comparing)) + " " + ToUtf8(m_cmpSeq->displayName());
    if (m_cmpMissing > 0) msg += "  \xC2\xB7  " + std::to_string(m_cmpMissing) + " " + tr(S::FramesMissing);
    showToast(msg, m_cmpMissing > 0);
}

void App::drawCompareMenu()
{
    const float s = m_dpiScale;
    const bool on = compareActive();
    if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.29f, 0.56f, 1.0f, 0.45f));
    ImGui::BeginDisabled(stackActive());
    if (ImGui::Button("A/B")) ImGui::OpenPopup("##compare");
    ImGui::EndDisabled();
    if (on) ImGui::PopStyleColor();
    ImGui::SetItemTooltip("%s", stackActive() ? tr(S::StackNoCrypto) : tr(S::CompareTitle));
    if (!ImGui::BeginPopup("##compare")) return;
    if (ImGui::IsWindowAppearing()) m_versions = FindVersions(*m_seq);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 360 * s);

    if (on) {
        ImGui::TextDisabled("B");
        ImGui::SameLine();
        ImGui::TextUnformatted(ToUtf8(m_cmpSeq->displayName()).c_str());
        ImGui::SetItemTooltip("%s", ToUtf8(m_cmpSeq->directory).c_str());
        if (m_cmpMissing > 0) ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.35f, 1), "%d %s", m_cmpMissing, tr(S::FramesMissing));
        ImGui::Spacing();
        static const S names[] = { S::CompareWipe, S::CompareSide, S::CompareDiff, S::CompareToggle };
        for (int m = 0; m < (int)CompareMode::Count; ++m) {
            if (ImGui::RadioButton(tr(names[m]), (int)m_cmpMode == m)) m_cmpMode = (CompareMode)m;
            if (m + 1 < (int)CompareMode::Count) ImGui::SameLine();
        }
        if (m_cmpMode == CompareMode::Difference) {
            ImGui::SetNextItemWidth(200 * s);
            ImGui::SliderFloat(tr(S::CompareGain), &m_diffGain, 1.0f, 1000.0f, "\xC3\x97%.0f", ImGuiSliderFlags_Logarithmic);
            ImGui::TextDisabled("%s", tr(S::CompareDiffHint));
        }
        ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);
        if (ImGui::MenuItem(tr(S::CompareSwap), "X", m_cmpSwap)) m_cmpSwap = !m_cmpSwap;
        ImGui::PopItemFlag();
        ImGui::Separator();
    }
    if (ImGui::MenuItem(on ? tr(S::CompareChange) : tr(S::CompareWith))) {
        defer([this] {
            const std::wstring p = ShowOpenFileDialog(m_hwnd, false, FromUtf8(tr(S::CompareWith)).c_str());
            if (!p.empty()) setCompare(p);
        });
    }
    std::wstring pick;
    if (ImGui::BeginMenu(tr(S::CompareVersion), m_versions.size() >= 2)) {
        for (auto it = m_versions.rbegin(); it != m_versions.rend(); ++it) {
            if (it->current) continue;
            char item[128];
            snprintf(item, sizeof(item), "%-8s  %d %s", ToUtf8(it->token).c_str(), it->frames, tr(S::Frames));
            ImGui::PushID(it->number);
            const bool isB = on && ToLower(it->path) == ToLower(m_cmpSeq->frames[0].path);
            if (ImGui::MenuItem(item, nullptr, isB)) pick = it->path;
            ImGui::PopID();
        }
        ImGui::EndMenu();
    }
    if (!pick.empty()) setCompare(pick);
    if (on && ImGui::MenuItem(tr(S::CompareRemove))) clearCompare();
    ImGui::Spacing();
    ImGui::TextDisabled("%s", tr(S::CompareHint));
    ImGui::PopTextWrapPos();
    ImGui::EndPopup();
}
