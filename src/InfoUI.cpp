// Metadata panel: the header of the current frame's file (EXR attributes, TIFF tags).
#include "App.h"
#include "I18n.h"
#include "Platform.h"

#include <windows.h>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cfloat>

namespace {

bool Matches(const std::string& text, const char* filter)
{
    if (!filter || !*filter) return true;
    const size_t n = strlen(filter);
    return std::search(text.begin(), text.end(), filter, filter + n,
                       [](char a, char b) { return tolower((unsigned char)a) == tolower((unsigned char)b); }) != text.end();
}

double Seconds()
{
    static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return double(t.QuadPart) / double(freq.QuadPart);
}

}  // namespace

void App::drawInfoPanel(float x, float y, float w, float h)
{
    const float s = m_dpiScale;
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14 * s, 12 * s));
    ImGui::Begin("##info", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar();

    ImGui::TextUnformatted(tr(S::Metadata));
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetStyle().WindowPadding.x - ImGui::GetFrameHeight() * 0.6f);
    if (ImGui::SmallButton("x")) m_infoPanel = false;

    // Read again when the frame (or its file) changes; a few times a second while playing.
    // Only once the frame is decoded: the file is then local (an online-only cloud file would
    // otherwise download here, on the UI thread) and in the OS cache.
    const SequenceFrame& f = m_seq->frames[m_index];
    const double now = Seconds();
    if ((f.path != m_metaPath || f.stamp != m_metaStamp) && (m_playDir == 0 || now - m_metaAt > 0.25) && m_cache.get(m_index)) {
        m_meta = ReadMetadata(f.path);
        m_metaPath = f.path;
        m_metaStamp = f.stamp;
        m_metaAt = now;
    }
    ImGui::TextDisabled("%s %d  \xC2\xB7  %s", tr(S::Frame), f.number, tr(S::MetaHint));
    ImGui::Spacing();
    const float copyW = ImGui::CalcTextSize(tr(S::CopyAll)).x + ImGui::GetStyle().FramePadding.x * 2;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - copyW - ImGui::GetStyle().ItemSpacing.x);
    ImGui::InputTextWithHint("##metafilter", tr(S::Search), m_metaFilter, sizeof(m_metaFilter));
    ImGui::SameLine();
    if (ImGui::Button(tr(S::CopyAll))) {
        std::string text;
        for (const MetaEntry& e : m_meta) text += e.section ? "\n[" + e.key + "]\n" : e.key + ": " + e.value + "\n";
        if (SetClipboardText(m_hwnd, text)) showToast(tr(S::CopiedText));
    }
    ImGui::Spacing();

    ImGui::BeginChild("##metalist", ImVec2(0, 0), ImGuiChildFlags_Borders);
    if (ImGui::BeginTable("##meta", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("key", ImGuiTableColumnFlags_WidthStretch, 0.42f);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 0.58f);
        const ImVec4 accent(0.45f, 0.65f, 1.0f, 1);
        for (size_t i = 0; i < m_meta.size(); ++i) {
            const MetaEntry& e = m_meta[i];
            if (e.section) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(accent, "%s", e.key.c_str());
                continue;
            }
            if (!Matches(e.key, m_metaFilter) && !Matches(e.value, m_metaFilter)) continue;
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushTextWrapPos(0);
            ImGui::TextDisabled("%s", e.key.c_str());
            ImGui::PopTextWrapPos();
            ImGui::TableSetColumnIndex(1);
            ImGui::PushID((int)i);
            ImGui::PushTextWrapPos(0);
            ImGui::TextUnformatted(e.value.c_str());
            ImGui::PopTextWrapPos();
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && SetClipboardText(m_hwnd, e.value)) showToast(tr(S::CopiedText));
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
    ImGui::End();
}
