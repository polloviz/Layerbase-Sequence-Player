#include "App.h"
#include "I18n.h"
#include "ImageIO.h"
#include "Platform.h"
#include "Version.h"

#include <windows.h>
#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>

namespace {

const ImU32 kAccent = IM_COL32(74, 143, 255, 255);
const ImU32 kAccentDim = IM_COL32(74, 143, 255, 110);
const ImU32 kText = IM_COL32(230, 230, 235, 255);
const ImU32 kTextDim = IM_COL32(135, 135, 145, 255);
const ImU32 kError = IM_COL32(255, 92, 92, 255);

enum class Icon { Menu, Play, Pause, PlayReverse, StepBack, StepForward, First, Last, MarkIn, MarkOut, Copy, Save, Folder };

bool IconButton(const char* id, Icon icon, float size, bool active, const char* tooltip)
{
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(size, size));
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hovered || ImGui::IsItemActive())
        dl->AddRectFilled(p, ImVec2(p.x + size, p.y + size),
                          ImGui::GetColorU32(ImGui::IsItemActive() ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered),
                          ImGui::GetStyle().FrameRounding);
    const ImU32 c = ImGui::GetColorU32(active ? kAccent : kText);   // dimmed when disabled
    const float cx = p.x + size * 0.5f, cy = p.y + size * 0.5f, r = size * 0.2f;
    auto tri = [&](float dir, float ox) {   // dir: +1 right, -1 left
        dl->AddTriangleFilled(ImVec2(cx + ox - r * 0.85f * dir, cy - r), ImVec2(cx + ox - r * 0.85f * dir, cy + r),
                              ImVec2(cx + ox + r * dir, cy), c);
    };
    auto bar = [&](float x) { dl->AddRectFilled(ImVec2(x - r * 0.18f, cy - r), ImVec2(x + r * 0.18f, cy + r), c); };
    switch (icon) {
    case Icon::Menu:
        for (int i = -1; i <= 1; ++i)
            dl->AddRectFilled(ImVec2(cx - r * 1.1f, cy + i * r * 0.7f - r * 0.1f), ImVec2(cx + r * 1.1f, cy + i * r * 0.7f + r * 0.1f), c, 1.0f);
        break;
    case Icon::Play: tri(1, r * 0.1f); break;
    case Icon::PlayReverse: tri(-1, -r * 0.1f); break;
    case Icon::Pause: bar(cx - r * 0.45f); bar(cx + r * 0.45f); break;
    case Icon::StepForward: tri(1, -r * 0.3f); bar(cx + r * 1.05f); break;
    case Icon::StepBack: tri(-1, r * 0.3f); bar(cx - r * 1.05f); break;
    case Icon::Last: tri(1, -r * 0.9f); tri(1, r * 0.35f); bar(cx + r * 1.5f); break;
    case Icon::First: tri(-1, r * 0.9f); tri(-1, -r * 0.35f); bar(cx - r * 1.5f); break;
    case Icon::MarkIn:
    case Icon::MarkOut: {   // bracket "[" / "]" with a small arrow pointing into the range
        const float d = icon == Icon::MarkIn ? 1.0f : -1.0f, bx = cx - d * r * 0.8f, t = r * 0.18f;
        dl->AddRectFilled(ImVec2(bx - t, cy - r * 1.1f), ImVec2(bx + t, cy + r * 1.1f), c);
        dl->AddRectFilled(ImVec2(std::min(bx, bx + d * r * 0.7f), cy - r * 1.1f), ImVec2(std::max(bx, bx + d * r * 0.7f), cy - r * 1.1f + 2 * t), c);
        dl->AddRectFilled(ImVec2(std::min(bx, bx + d * r * 0.7f), cy + r * 1.1f - 2 * t), ImVec2(std::max(bx, bx + d * r * 0.7f), cy + r * 1.1f), c);
        const float ax = bx + d * r * 0.55f;
        dl->AddTriangleFilled(ImVec2(ax, cy - r * 0.5f), ImVec2(ax, cy + r * 0.5f), ImVec2(ax + d * r * 0.9f, cy), c);
        break;
    }
    case Icon::Copy: {   // two sheets
        const float h = r * 1.1f, t = std::max(1.0f, size * 0.05f);
        dl->AddRect(ImVec2(cx - h, cy - h), ImVec2(cx + h * 0.4f, cy + h * 0.4f), c, t, 0, t);
        dl->AddRectFilled(ImVec2(cx - h * 0.4f, cy - h * 0.4f), ImVec2(cx + h, cy + h), c, t);
        break;
    }
    case Icon::Save: {   // arrow into a tray
        const float h = r * 1.1f, t = std::max(1.0f, size * 0.05f);
        const ImVec2 tray[] = { ImVec2(cx - h, cy + h * 0.2f), ImVec2(cx - h, cy + h), ImVec2(cx + h, cy + h), ImVec2(cx + h, cy + h * 0.2f) };
        dl->AddPolyline(tray, 4, c, 0, t);
        dl->AddRectFilled(ImVec2(cx - t * 0.5f, cy - h), ImVec2(cx + t * 0.5f, cy), c);
        dl->AddTriangleFilled(ImVec2(cx - h * 0.5f, cy - h * 0.1f), ImVec2(cx + h * 0.5f, cy - h * 0.1f), ImVec2(cx, cy + h * 0.45f), c);
        break;
    }
    case Icon::Folder: {
        const float h = r * 1.1f, t = std::max(1.0f, size * 0.05f);
        const ImVec2 pts[] = { ImVec2(cx - h, cy - h * 0.8f), ImVec2(cx - h * 0.2f, cy - h * 0.8f), ImVec2(cx + h * 0.05f, cy - h * 0.5f),
                               ImVec2(cx + h, cy - h * 0.5f), ImVec2(cx + h, cy + h * 0.8f), ImVec2(cx - h, cy + h * 0.8f) };
        dl->AddPolyline(pts, 6, c, ImDrawFlags_Closed, t);
        dl->AddLine(ImVec2(cx - h, cy - h * 0.2f), ImVec2(cx + h, cy - h * 0.2f), c, t);
        break;
    }
    }
    if (tooltip) ImGui::SetItemTooltip("%s", tooltip);
    return pressed;
}

bool ContainsNoCase(const std::string& hay, const char* needle)
{
    if (!needle || !*needle) return true;
    auto it = std::search(hay.begin(), hay.end(), needle, needle + strlen(needle),
                          [](char a, char b) { return tolower((unsigned char)a) == tolower((unsigned char)b); });
    return it != hay.end();
}

void DimLabel(const char* text)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", text);
    ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
}

float DimLabelWidth(const char* text)
{
    return ImGui::CalcTextSize(text).x + ImGui::GetStyle().ItemInnerSpacing.x;
}

std::string FormatFps(double fps)
{
    char buf[32];
    if (std::fabs(fps - std::round(fps)) < 1e-3) snprintf(buf, sizeof(buf), "%d", (int)std::round(fps));
    else snprintf(buf, sizeof(buf), "%.3f", fps);
    std::string s = buf;
    if (s.find('.') != std::string::npos) {
        while (s.back() == '0') s.pop_back();
        if (s.back() == '.') s.pop_back();
    }
    return s;
}

}  // namespace

// ---------------------------------------------------------------------------

void App::drawUI()
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    if (m_uiVisible) {
        drawTopBar();
        drawBottomBar();
    } else {
        m_topBarH = m_bottomBarH = 0;
    }
    drawSettings();
    drawAbout();
    drawExportDialog();
    drawBatchDialog();
    drawExportProgress();
    drawReplaceConfirm();

    const float vh = std::max(1.0f, vp->Size.y - m_topBarH - m_bottomBarH);
    const bool panel = m_uiVisible && m_seq && (m_cryptoPanel || m_stackPanel);
    m_panelW = panel ? std::round((m_stackPanel ? 330 : 310) * m_dpiScale) : 0.0f;
    if (m_panelW > 0) {
        const float px = vp->Pos.x + vp->Size.x - m_panelW, py = vp->Pos.y + m_topBarH;
        if (m_stackPanel) drawStackPanel(px, py, m_panelW, vh);
        else drawCryptoPanel(px, py, m_panelW, vh);
    }
    const float vx = vp->Pos.x, vy = vp->Pos.y + m_topBarH;
    const float vw = std::max(1.0f, vp->Size.x - m_panelW);
    handleViewerInput(vx, vy, vw, vh);
    if (!m_seq) drawEmptyState(vx, vy, vw, vh);
    drawOverlays(vx, vy, vw, vh);
    pollUpdateCheck();
    drawUpdateBanner();
}

void App::drawTopBar()
{
    const float s = m_dpiScale;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float h = std::round(46 * s);
    m_topBarH = h;
    const float frameH = ImGui::GetFrameHeight();

    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10 * s, std::floor((h - frameH) * 0.5f)));
    ImGui::Begin("##top", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                    ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();

    if (IconButton("##menu", Icon::Menu, frameH, false, nullptr)) ImGui::OpenPopup("MainMenu");
    drawMainMenu();

    // Color pipeline, left to right: config -> input -> display -> view -> look.
    // Widths shrink proportionally when the window is narrow.
    const ImGuiStyle& st = ImGui::GetStyle();
    const bool hasLooks = !m_color.looks().empty();
    float w[5] = { 190 * s, 230 * s, 160 * s, 260 * s, hasLooks ? 150 * s : 0 };
    const float wExp = 96 * s, wGam = 76 * s, wLut = 50 * s, wToggle = 58 * s;
    const float labels = DimLabelWidth(tr(S::Input)) + DimLabelWidth(tr(S::Display)) + DimLabelWidth(tr(S::View)) +
                         (hasLooks ? DimLabelWidth(tr(S::Look)) : 0);
    const float windowW = ImGui::GetWindowWidth();
    const float fixedRight = wExp + wGam + wLut + wToggle + st.ItemSpacing.x * 4;
    const float avail = windowW - 2 * st.WindowPadding.x - frameH - labels - fixedRight - st.ItemSpacing.x * (hasLooks ? 6 : 5) - 16 * s;
    const float want = w[0] + w[1] + w[2] + w[3] + w[4];
    if (want > avail) {
        const float k = std::max(0.35f, avail / want);
        for (float& x : w) x *= k;
    }

    ImGui::SameLine();
    drawConfigCombo(w[0]);
    ImGui::SameLine(0, st.ItemSpacing.x * 2);
    DimLabel(tr(S::Input));
    std::string picked;
    if (stackActive()) {   // the selected stack layer
        const int sel = std::clamp(m_stackSel, 0, (int)m_stack.size() - 1);
        if (drawColorSpaceCombo("##input", w[1], m_stack[sel].input, m_stack[sel].inputAuto, picked)) stackSetInput(sel, picked);
    } else if (drawColorSpaceCombo("##input", w[1], m_color.input, !m_inputUserChosen, picked)) {
        setInput(picked, true);
    }
    ImGui::SameLine();
    drawDisplayViewCombos(w[2], w[3], w[4]);

    ImGui::SameLine(windowW - st.WindowPadding.x - fixedRight + st.ItemSpacing.x);
    ImGui::SetNextItemWidth(wExp);
    ImGui::DragFloat("##exposure", &m_exposure, 0.02f, -16.0f, 16.0f, "EV %+.2f");
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) m_exposure = 0.0f;
    ImGui::SetItemTooltip("%s  (- / +)", tr(S::Exposure));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(wGam);
    ImGui::DragFloat("##gamma", &m_gamma, 0.005f, 0.1f, 4.0f, "\xCE\xB3 %.2f");
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) m_gamma = 1.0f;
    ImGui::SetItemTooltip("%s", tr(S::Gamma));

    ImGui::SameLine();
    drawLutButton(wLut);

    ImGui::SameLine();
    const bool on = m_colorManaged;
    if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(kAccentDim));
    if (ImGui::Button("OCIO", ImVec2(wToggle, 0))) {
        m_colorManaged = !m_colorManaged;
        m_colorDirty = true;
    }
    if (on) ImGui::PopStyleColor();
    ImGui::SetItemTooltip("%s", tr(S::ColorManaged));

    ImGui::End();
}

void App::drawConfigCombo(float width)
{
    const float s = m_dpiScale;
    ImGui::SetNextItemWidth(width);
    ImGui::SetNextWindowSizeConstraints(ImVec2(380 * s, 0), ImVec2(FLT_MAX, 520 * s));
    const std::string preview = m_color.valid() ? m_color.sourceLabel() : "-";
    if (ImGui::BeginCombo("##config", preview.c_str(), ImGuiComboFlags_HeightLargest)) {
        ImGui::SeparatorText(tr(S::BuiltinConfigs));
        for (const auto& b : ColorManager::Builtins()) {
            const bool sel = m_color.source() == b.uri;
            if (ImGui::Selectable(b.label.c_str(), sel)) loadConfig(b.uri);
            ImGui::SetItemTooltip("%s\n%s", b.uiName.c_str(), b.uri.c_str());
        }
        if (!ColorManager::BlenderConfigs().empty()) {
            ImGui::SeparatorText(tr(S::BlenderConfigs));
            for (const auto& b : ColorManager::BlenderConfigs()) {
                ImGui::PushID(b.path.c_str());
                if (ImGui::Selectable((b.label + "  (AgX, Filmic, ACES)").c_str(), m_color.source() == b.path)) loadConfig(b.path);
                ImGui::SetItemTooltip("%s", b.path.c_str());
                ImGui::PopID();
            }
        }
        if (ColorManager::HasEnvConfig()) {
            ImGui::Separator();
            if (ImGui::Selectable(tr(S::EnvConfig), m_color.source() == "$OCIO")) loadConfig("$OCIO");
            ImGui::SetItemTooltip("%s", getenv("OCIO"));
        }
        ImGui::Separator();
        if (ImGui::Selectable(tr(S::LoadCustomConfig))) defer([this] { loadCustomConfigDialog(); });
        if (!m_settings.recentConfigs.empty()) {
            ImGui::SeparatorText(tr(S::RecentConfigs));
            std::string toLoad;
            for (const auto& path : m_settings.recentConfigs) {
                const std::wstring w = FromUtf8(path);
                std::string label = ToUtf8(GetFileName(GetParentDir(w))) + " / " + ToUtf8(GetFileName(w));
                ImGui::PushID(path.c_str());
                if (ImGui::Selectable(label.c_str(), m_color.source() == path)) toLoad = path;
                ImGui::SetItemTooltip("%s", path.c_str());
                ImGui::PopID();
            }
            if (!toLoad.empty()) loadConfig(toLoad);
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("%s: %s", tr(S::Config), m_color.source().c_str());
}

bool App::drawColorSpaceCombo(const char* id, float width, const std::string& current, bool isAuto, std::string& picked)
{
    const float s = m_dpiScale;
    bool changed = false;
    ImGui::SetNextItemWidth(width);
    ImGui::SetNextWindowSizeConstraints(ImVec2(std::max(width, 340 * s), 0), ImVec2(FLT_MAX, FLT_MAX));
    const std::string preview = current.empty() ? "-" : current;
    if (ImGui::BeginCombo(id, preview.c_str(), ImGuiComboFlags_HeightLargest)) {
        if (ImGui::IsWindowAppearing()) {
            m_filter[0] = 0;
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##filter", tr(S::Search), m_filter, sizeof(m_filter));
        if (ImGui::Selectable(tr(S::AutoDetect), isAuto)) { picked.clear(); changed = true; }
        ImGui::Separator();

        ImGui::BeginChild("##list", ImVec2(0, 380 * s), ImGuiChildFlags_None);
        // Group by family in first-seen order.
        std::vector<std::string> families;
        for (auto& cs : m_color.colorSpaces())
            if (std::find(families.begin(), families.end(), cs.family) == families.end()) families.push_back(cs.family);
        const bool filtering = m_filter[0] != 0;
        for (const auto& fam : families) {
            bool header = false;
            for (const auto& cs : m_color.colorSpaces()) {
                if (cs.family != fam) continue;
                if (filtering && !ContainsNoCase(cs.name, m_filter) && !ContainsNoCase(cs.family, m_filter)) continue;
                if (!header && !fam.empty()) {
                    ImGui::Spacing();
                    ImGui::TextDisabled("%s", fam.c_str());
                    header = true;
                }
                const bool sel = cs.name == current;
                if (ImGui::Selectable(("  " + cs.name).c_str(), sel)) {
                    picked = cs.name;
                    changed = true;
                    ImGui::CloseCurrentPopup();
                }
                if (sel && ImGui::IsWindowAppearing()) ImGui::SetScrollHereY();
                if (!cs.description.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(420 * s, FLT_MAX));
                    ImGui::BeginTooltip();
                    ImGui::PushTextWrapPos(400 * s);
                    ImGui::TextUnformatted(cs.description.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }
            }
        }
        ImGui::EndChild();
        ImGui::EndCombo();
    }
    if (const ColorSpaceInfo* cs = m_color.findColorSpace(current))
        ImGui::SetItemTooltip("%s: %s%s", tr(S::Input), cs->name.c_str(), isAuto ? "  (auto)" : "");
    return changed;
}

void App::drawDisplayViewCombos(float wDisplay, float wView, float wLook)
{
    DimLabel(tr(S::Display));
    ImGui::SetNextItemWidth(wDisplay);
    if (ImGui::BeginCombo("##display", m_color.display.c_str(), ImGuiComboFlags_HeightLarge)) {
        for (const auto& d : m_color.displays())
            if (ImGui::Selectable(d.c_str(), d == m_color.display)) {
                m_color.display = d;
                auto v = m_color.views(d);
                if (std::find(v.begin(), v.end(), m_color.view) == v.end()) m_color.view = m_color.defaultView(d);
                m_colorDirty = true;
            }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("%s: %s", tr(S::Display), m_color.display.c_str());

    ImGui::SameLine();
    DimLabel(tr(S::View));
    ImGui::SetNextItemWidth(wView);
    ImGui::SetNextWindowSizeConstraints(ImVec2(wView, 0), ImVec2(FLT_MAX, FLT_MAX));
    if (ImGui::BeginCombo("##view", m_color.view.c_str(), ImGuiComboFlags_HeightLarge)) {
        for (const auto& v : m_color.views(m_color.display))
            if (ImGui::Selectable(v.c_str(), v == m_color.view)) {
                m_color.view = v;
                m_colorDirty = true;
            }
        ImGui::SeparatorText("AgX");
        for (const auto& v : ColorManager::AgxViewNames()) {
            if (ImGui::Selectable(v.c_str(), v == m_color.view)) {
                m_color.view = v;
                m_colorDirty = true;
            }
            ImGui::SetItemTooltip("%s", tr(S::AgxTooltip));
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("%s: %s", tr(S::View), m_color.view.c_str());

    if (wLook > 0) {
        ImGui::SameLine();
        DimLabel(tr(S::Look));
        ImGui::SetNextItemWidth(wLook);
        if (ImGui::BeginCombo("##look", m_color.look.empty() ? tr(S::NoLook) : m_color.look.c_str(), ImGuiComboFlags_HeightLarge)) {
            if (ImGui::Selectable(tr(S::NoLook), m_color.look.empty())) { m_color.look.clear(); m_colorDirty = true; }
            for (const auto& l : m_color.looks())
                if (ImGui::Selectable(l.c_str(), l == m_color.look)) { m_color.look = l; m_colorDirty = true; }
            ImGui::EndCombo();
        }
    }
}

void App::drawMainMenu()
{
    if (!ImGui::BeginPopup("MainMenu")) return;
    if (ImGui::MenuItem(tr(S::Open), "Ctrl+O")) defer([this] { openDialog(false); });
    if (ImGui::MenuItem(tr(S::OpenFolder), "Ctrl+Shift+O")) defer([this] { openDialog(true); });
    if (ImGui::BeginMenu(tr(S::Recent))) {
        if (m_settings.recentFiles.empty()) ImGui::TextDisabled("%s", tr(S::NoRecent));
        std::string toOpen;
        for (const auto& f : m_settings.recentFiles) {
            ImGui::PushID(f.c_str());
            if (ImGui::MenuItem(ToUtf8(GetFileName(FromUtf8(f))).c_str())) toOpen = f;
            ImGui::SetItemTooltip("%s", f.c_str());
            ImGui::PopID();
        }
        if (!toOpen.empty()) defer([this, toOpen] { requestOpen(FromUtf8(toOpen)); });
        ImGui::EndMenu();
    }
    ImGui::Separator();
    const bool frameReady = m_seq && m_shown && m_shown->valid();
    if (ImGui::MenuItem(tr(S::CopyFrame), "Ctrl+C", false, frameReady)) defer([this] { copyFrame(); });
    if (ImGui::MenuItem(tr(S::SaveFrame), "Ctrl+S", false, frameReady)) defer([this] { saveFrameDialog(); });
    if (ImGui::MenuItem(tr(S::RevealFrame), "Ctrl+Shift+R", false, m_seq != nullptr)) revealFrame();
    ImGui::Separator();
    if (ImGui::MenuItem(tr(S::ExportMovie), "Ctrl+E", false, frameReady)) openExportDialog();
    if (ImGui::MenuItem(tr(S::BatchMenu), "Ctrl+B")) openBatchDialog();
    if (ImGui::MenuItem(tr(S::LoadMatte), nullptr, false, m_seq != nullptr && !stackActive())) defer([this] { loadMatteDialog(); });
    ImGui::Separator();
    if (ImGui::MenuItem(tr(S::LoadCustomConfig))) defer([this] { loadCustomConfigDialog(); });
    if (ImGui::MenuItem(tr(S::LoadLut))) defer([this] { loadLutDialog(); });
    ImGui::Separator();
    if (ImGui::MenuItem(tr(S::Settings))) m_openSettings = true;
    if (ImGui::MenuItem(tr(S::AboutTitle))) m_openAbout = true;
    if (ImGui::MenuItem(tr(S::Exit))) PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------

void App::drawBottomBar()
{
    const float s = m_dpiScale;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float timelineH = std::round(26 * s);
    const float h = std::round(timelineH + ImGui::GetFrameHeight() + 26 * s);
    m_bottomBarH = h;

    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + vp->Size.y - h));
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12 * s, 6 * s));
    ImGui::Begin("##bottom", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    drawTimeline(ImGui::GetContentRegionAvail().x, timelineH);
    drawTransport();
    ImGui::End();
}

void App::drawTimeline(float width, float height)
{
    const float s = m_dpiScale;
    const int n = frameCount();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##timeline", ImVec2(width, height));
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const float trackY0 = p.y + height * 0.5f - 3 * s, trackY1 = p.y + height * 0.5f + 3 * s;
    dl->AddRectFilled(ImVec2(p.x, trackY0), ImVec2(p.x + width, trackY1), IM_COL32(255, 255, 255, 18), 3 * s);
    if (n <= 0) return;

    const float cell = width / n;
    auto xOf = [&](int i) { return p.x + i * cell; };

    // In/out range
    if (m_in > 0 || m_out < n - 1) {
        dl->AddRectFilled(ImVec2(xOf(0), p.y), ImVec2(xOf(m_in), p.y + height), IM_COL32(0, 0, 0, 90));
        dl->AddRectFilled(ImVec2(xOf(m_out + 1), p.y), ImVec2(xOf(n), p.y + height), IM_COL32(0, 0, 0, 90));
        dl->AddLine(ImVec2(xOf(m_in), p.y + 2 * s), ImVec2(xOf(m_in), p.y + height - 2 * s), IM_COL32(255, 200, 80, 200), 2 * s);
        dl->AddLine(ImVec2(xOf(m_out + 1), p.y + 2 * s), ImVec2(xOf(m_out + 1), p.y + height - 2 * s), IM_COL32(255, 200, 80, 200), 2 * s);
    }

    // Cached / error runs
    if ((int)m_cacheMask.size() == n) {
        int i = 0;
        while (i < n) {
            const uint8_t v = m_cacheMask[i];
            int j = i;
            while (j < n && m_cacheMask[j] == v) ++j;
            if (v)
                dl->AddRectFilled(ImVec2(xOf(i), trackY0), ImVec2(std::max(xOf(j), xOf(i) + 1), trackY1),
                                  v == 1 ? kAccentDim : kError, 0);
            i = j;
        }
    }

    // Scrubbing
    if (ImGui::IsItemActivated()) {
        m_scrubResumeDir = m_playDir;
        m_playDir = 0;
        m_scrubbing = true;
    }
    if (ImGui::IsItemActive()) {
        const int idx = std::clamp((int)((ImGui::GetIO().MousePos.x - p.x) / cell), 0, n - 1);
        seek(idx);
    }
    if (ImGui::IsItemDeactivated()) {
        m_scrubbing = false;
        if (m_scrubResumeDir) setPlaying(m_scrubResumeDir);
    }
    if (ImGui::IsItemHovered() && !ImGui::IsItemActive()) {
        const int idx = std::clamp((int)((ImGui::GetIO().MousePos.x - p.x) / cell), 0, n - 1);
        const float hx = xOf(idx) + cell * 0.5f;
        dl->AddLine(ImVec2(hx, p.y + 4 * s), ImVec2(hx, p.y + height - 4 * s), IM_COL32(255, 255, 255, 70), 1.0f);
        ImGui::SetTooltip("%d", m_seq->frames[idx].number);
    }

    // Playhead
    const float x = xOf(m_index) + cell * 0.5f;
    dl->AddRectFilled(ImVec2(x - 1.0f * s, p.y + 2 * s), ImVec2(x + 1.0f * s, p.y + height - 2 * s), kAccent, 1 * s);
    dl->AddCircleFilled(ImVec2(x, p.y + height * 0.5f), 5 * s, kAccent);
}

void App::drawTransport()
{
    const float s = m_dpiScale;
    const ImGuiStyle& st = ImGui::GetStyle();
    const float fh = ImGui::GetFrameHeight();
    const float windowW = ImGui::GetWindowWidth();
    const int n = frameCount();

    // --- left: frame counter ---
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2 * s);
    const float rowY = ImGui::GetCursorPosY();
    if (n > 0) {
        int number = m_seq->frames[m_index].number;
        ImGui::SetNextItemWidth(72 * s);
        ImGui::PushFont(m_fontMono, 0.0f);
        if (ImGui::InputInt("##frame", &number, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue)) {
            int best = 0;
            for (int i = 0; i < n; ++i)
                if (std::abs(m_seq->frames[i].number - number) < std::abs(m_seq->frames[best].number - number)) best = i;
            m_playDir = 0;
            seek(best);
        }
        ImGui::PopFont();
        ImGui::SetItemTooltip("%s", tr(S::Frame));
        ImGui::SameLine();
        const bool frameReady = m_shown && m_shown->valid();
        std::string tip = std::string(tr(S::CopyFrame)) + "  (Ctrl+C)\n" + tr(S::FrameGrabHint);
        ImGui::BeginDisabled(!frameReady);
        if (IconButton("##copyframe", Icon::Copy, fh, false, tip.c_str())) defer([this] { copyFrame(); });
        ImGui::SameLine(0, 2 * s);
        tip = std::string(tr(S::SaveFrame)) + "  (Ctrl+S)\n" + tr(S::FrameGrabHint);
        if (IconButton("##saveframe", Icon::Save, fh, false, tip.c_str())) defer([this] { saveFrameDialog(); });
        ImGui::EndDisabled();
        ImGui::SameLine(0, 2 * s);
        tip = std::string(tr(S::RevealFrame)) + "  (Ctrl+Shift+R)";
        if (IconButton("##revealframe", Icon::Folder, fh, false, tip.c_str())) revealFrame();
        ImGui::SameLine();
        // Info text is clipped before the centered transport buttons.
        const float clipX = ImGui::GetWindowPos().x + std::floor((windowW - (fh + 4 * s) * 8.8f) * 0.5f) - 12 * s;
        const ImVec2 cp = ImGui::GetCursorScreenPos();
        ImGui::PushClipRect(cp, ImVec2(std::max(cp.x, clipX), cp.y + fh), true);
        ImGui::AlignTextToFramePadding();
        if (m_in > 0 || m_out < n - 1) {
            ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.31f, 1), "%s %d\xE2\x80\x93%d  \xC3\x97", tr(S::InOut),
                               m_seq->frames[m_in].number, m_seq->frames[m_out].number);
            if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (ImGui::IsItemClicked()) { m_in = 0; m_out = n - 1; }
            ImGui::SetItemTooltip("%s (U)", tr(S::ClearInOut));
            ImGui::SameLine();
        }
        ImGui::TextDisabled("%d \xE2\x80\x93 %d  \xC2\xB7  %d %s", m_seq->frames.front().number, m_seq->frames.back().number, n, tr(S::Frames));
        if (m_shown && m_shown->valid()) {
            ImGui::SameLine();
            const std::string what = stackActive() ? std::string(tr(S::StackTitle)) + " (" + std::to_string(m_stack.size()) + ")"
                                                   : m_shown->description;
            ImGui::TextDisabled("\xC2\xB7  %d\xC3\x97%d%s  \xC2\xB7  %s", m_shown->fullWidth(), m_shown->fullHeight(),
                                m_planProxy > 1 ? (m_planProxy == 2 ? "  (1:2)" : "  (1:4)") : "", what.c_str());
        }
        ImGui::PopClipRect();
    }

    // --- center: transport ---
    const float bs = fh + 4 * s;
    const float gap = 10 * s;   // separates the in/out buttons from the transport
    const float groupW = bs * 8 + st.ItemSpacing.x * 0.5f * 5 + gap * 2;
    ImGui::SetCursorPos(ImVec2(std::floor((windowW - groupW) * 0.5f), rowY - 2 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(st.ItemSpacing.x * 0.5f, st.ItemSpacing.y));
    if (IconButton("##markin", Icon::MarkIn, bs, n > 0 && m_in > 0, tr(S::SetIn)) && n > 0) {
        m_in = m_index;
        m_out = std::max(m_out, m_in);
    }
    ImGui::SameLine(0, gap);
    if (IconButton("##first", Icon::First, bs, false, tr(S::FirstFrame))) { m_playDir = 0; seek(m_in); }
    ImGui::SameLine();
    if (IconButton("##back", Icon::StepBack, bs, false, tr(S::StepBack))) step(-1);
    ImGui::SameLine();
    if (IconButton("##rev", m_playDir < 0 ? Icon::Pause : Icon::PlayReverse, bs, m_playDir < 0, tr(S::PlayReverse)))
        setPlaying(m_playDir < 0 ? 0 : -1);
    ImGui::SameLine();
    if (IconButton("##play", m_playDir > 0 ? Icon::Pause : Icon::Play, bs, m_playDir > 0, m_playDir > 0 ? tr(S::Pause) : tr(S::Play)))
        setPlaying(m_playDir > 0 ? 0 : 1);
    ImGui::SameLine();
    if (IconButton("##fwd", Icon::StepForward, bs, false, tr(S::StepForward))) step(1);
    ImGui::SameLine();
    if (IconButton("##last", Icon::Last, bs, false, tr(S::LastFrame))) { m_playDir = 0; seek(m_out); }
    ImGui::SameLine(0, gap);
    if (IconButton("##markout", Icon::MarkOut, bs, n > 0 && m_out < n - 1, tr(S::SetOut)) && n > 0) {
        m_out = m_index;
        m_in = std::min(m_in, m_out);
    }
    ImGui::PopStyleVar();

    // --- right: loop, fps, channels, zoom ---
    const char* loopLabel = m_loop == LoopMode::Loop ? tr(S::Loop) : m_loop == LoopMode::Once ? tr(S::Once) : tr(S::PingPong);
    const std::string fpsLabel = FormatFps(m_fps) + " " + tr(S::Fps);
    static const char* channelNames[] = { "RGB", "R", "G", "B", "A", "Luma" };
    char zoomLabel[32];
    if (m_fit) snprintf(zoomLabel, sizeof(zoomLabel), "%s", tr(S::Fit));
    else snprintf(zoomLabel, sizeof(zoomLabel), "%.0f%%", m_zoom * 100.0f);

    const float wLoop = ImGui::CalcTextSize(loopLabel).x + st.FramePadding.x * 2;
    const float wFps = 92 * s, wRes = 52 * s, wCh = 64 * s, wZoom = 64 * s;
    const float wActual = m_playDir != 0 ? ImGui::CalcTextSize("000.0").x + st.ItemSpacing.x : 0;
    const bool layerCombo = hasLayers() && !stackActive();   // the stack picks layers per row
    const float wLayer = layerCombo ? 170 * s + st.ItemSpacing.x : 0;
    // Alpha interpretation of the opened sequence (not while its alpha carries a Cryptomatte mask).
    ImagePtr own = stackActive() ? nullptr : m_shown;
    for (const StackLayer& l : m_stack)
        if (!l.seq && m_shownSet) { own = m_shownSet->find(l.key); break; }
    const bool alphaCombo = own && own->valid() && own->hasAlpha && !(m_loadOpts && m_loadOpts->cryptoActive());
    const float wAlphaCombo = 128 * s;
    const float wAlpha = alphaCombo ? wAlphaCombo + st.ItemSpacing.x : 0;
    auto buttonW = [&](const char* t) { return ImGui::CalcTextSize(t).x + st.FramePadding.x * 2 + st.ItemSpacing.x; };
    const float wPanels = n > 0 ? buttonW(tr(S::Stack)) + buttonW("Cryptomatte") : 0;
    const float rightW = wLayer + wAlpha + wPanels + wLoop + wFps + wRes + wCh + wZoom + wActual + st.ItemSpacing.x * 4;
    ImGui::SetCursorPos(ImVec2(windowW - st.WindowPadding.x - rightW, rowY));

    if (layerCombo) {
        drawLayerCombo(170 * s);
        ImGui::SameLine();
    }
    if (alphaCombo) {
        drawAlphaCombo(wAlphaCombo);
        ImGui::SameLine();
    }
    if (n > 0) {
        const ImVec4 onColor(0.29f, 0.56f, 1.0f, 0.45f);
        const bool stackOn = m_stackPanel || stackActive();
        if (stackOn) ImGui::PushStyleColor(ImGuiCol_Button, onColor);
        if (ImGui::Button(tr(S::Stack))) {
            m_stackPanel = !m_stackPanel;
            if (m_stackPanel) m_cryptoPanel = false;
        }
        if (stackOn) ImGui::PopStyleColor();
        ImGui::SetItemTooltip("%s", tr(S::StackTitle));
        ImGui::SameLine();

        // Always available: a sequence without Cryptomatte can use an external one.
        const bool on = m_cryptoPanel || m_matte != MatteMode::Off;
        ImGui::BeginDisabled(stackActive());
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, onColor);
        else if (!hasCrypto()) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        if (ImGui::Button("Cryptomatte")) {
            m_cryptoPanel = !m_cryptoPanel;
            if (m_cryptoPanel) m_stackPanel = false;
        }
        if (on || !hasCrypto()) ImGui::PopStyleColor();
        ImGui::EndDisabled();
        if (stackActive()) ImGui::SetItemTooltip("%s", tr(S::StackNoCrypto));
        ImGui::SameLine();
    }

    if (m_playDir != 0) {
        ImGui::AlignTextToFramePadding();
        const bool slow = m_actualFps < m_fps * 0.95;
        ImGui::TextColored(slow ? ImVec4(1, 0.55f, 0.35f, 1) : ImVec4(0.5f, 0.5f, 0.55f, 1), "%5.1f", m_actualFps);
        ImGui::SetItemTooltip("%s %s", tr(S::Fps), tr(S::Actual));
        ImGui::SameLine();
    }

    if (ImGui::Button(loopLabel)) m_loop = LoopMode(((int)m_loop + 1) % 3);
    ImGui::SameLine();

    ImGui::SetNextItemWidth(wFps);
    if (ImGui::BeginCombo("##fps", fpsLabel.c_str(), ImGuiComboFlags_HeightLarge)) {
        static const double presets[] = { 12, 15, 23.976, 24, 25, 29.97, 30, 48, 50, 59.94, 60, 120 };
        for (double f : presets)
            if (ImGui::Selectable(FormatFps(f).c_str(), std::fabs(f - m_fps) < 1e-3)) {
                m_fps = f;
                if (m_playDir) setPlaying(m_playDir);
            }
        ImGui::Separator();
        ImGui::SetNextItemWidth(120 * s);
        double custom = m_fps;
        if (ImGui::InputDouble(tr(S::Custom), &custom, 0, 0, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue)) {
            m_fps = std::clamp(custom, 1.0, 240.0);
            if (m_playDir) setPlaying(m_playDir);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("%s", tr(S::FrameRate));
    ImGui::SameLine();

    // Playback proxy
    static const int proxies[] = { 1, 2, 4 };
    const char* proxyNames[] = { tr(S::ResFull), tr(S::ResHalf), tr(S::ResQuarter) };
    char proxyLabel[16];
    snprintf(proxyLabel, sizeof(proxyLabel), "1:%d", m_proxy);
    ImGui::SetNextItemWidth(wRes);
    if (ImGui::BeginCombo("##proxy", proxyLabel)) {
        for (int i = 0; i < 3; ++i) {
            char item[64];
            snprintf(item, sizeof(item), "1:%d  %s", proxies[i], proxyNames[i]);
            if (ImGui::Selectable(item, m_proxy == proxies[i])) m_proxy = proxies[i];   // applied by updatePlayback
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("%s", tr(S::Resolution));
    ImGui::SameLine();

    ImGui::SetNextItemWidth(wCh);
    if (ImGui::BeginCombo("##channels", channelNames[(int)m_channel])) {
        for (int i = 0; i < 6; ++i)
            if (ImGui::Selectable(channelNames[i], (int)m_channel == i)) m_channel = (ChannelMode)i;
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("%s (R G B A Y C)", tr(S::Channels));
    ImGui::SameLine();

    if (ImGui::Button(zoomLabel, ImVec2(wZoom, 0))) {
        if (m_fit) { m_fit = false; m_zoom = 1.0f; m_panX = m_panY = 0; }
        else m_fit = true;
    }
    ImGui::SetItemTooltip("F / 1");
}

// ---------------------------------------------------------------------------

void App::drawEmptyState(float vx, float vy, float vw, float vh)
{
    const float s = m_dpiScale;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImVec2 c(vx + vw * 0.5f, vy + vh * 0.5f - 20 * s);

    // Film frame glyph
    const float w = 84 * s, h = 60 * s, r = 8 * s;
    const ImU32 col = IM_COL32(255, 255, 255, 40);
    dl->AddRect(ImVec2(c.x - w / 2, c.y - h / 2 - 30 * s), ImVec2(c.x + w / 2, c.y + h / 2 - 30 * s), col, r, 0, 2 * s);
    for (int i = 0; i < 5; ++i) {
        const float x = c.x - w / 2 + 12 * s + i * (w - 24 * s) / 4;
        dl->AddRectFilled(ImVec2(x - 3 * s, c.y - h / 2 - 30 * s + 7 * s), ImVec2(x + 3 * s, c.y - h / 2 - 30 * s + 13 * s), col, 1.5f * s);
        dl->AddRectFilled(ImVec2(x - 3 * s, c.y + h / 2 - 30 * s - 13 * s), ImVec2(x + 3 * s, c.y + h / 2 - 30 * s - 7 * s), col, 1.5f * s);
    }
    dl->AddTriangleFilled(ImVec2(c.x - 8 * s, c.y - 30 * s - 10 * s), ImVec2(c.x - 8 * s, c.y - 30 * s + 10 * s),
                          ImVec2(c.x + 11 * s, c.y - 30 * s), IM_COL32(74, 143, 255, 160));

    const char* l1 = tr(S::DropHint);
    const char* l2 = tr(S::DropHint2);
    const float fs = ImGui::GetFontSize();
    ImVec2 t1 = ImGui::CalcTextSize(l1);
    dl->AddText(nullptr, fs * 1.25f, ImVec2(c.x - t1.x * 1.25f * 0.5f, c.y + 18 * s), kText, l1);
    ImVec2 t2 = ImGui::CalcTextSize(l2);
    dl->AddText(ImVec2(c.x - t2.x * 0.5f, c.y + 18 * s + fs * 1.25f + 8 * s), kTextDim, l2);
    char formats[256] = "EXR  DPX  TIFF  PNG  JPEG  TGA  BMP  HDR  PSD";
    ImVec2 t3 = ImGui::CalcTextSize(formats);
    dl->AddText(ImVec2(c.x - t3.x * 0.5f, c.y + 18 * s + fs * 2.25f + 22 * s), IM_COL32(255, 255, 255, 50), formats);
    const std::string by = std::string(tr(S::CreatedBy)) + " " + APP_PUBLISHER;
    const ImVec2 t4 = ImGui::CalcTextSize(by.c_str());
    dl->AddText(ImVec2(c.x - t4.x * 0.5f, vy + vh - t4.y - 16 * s), IM_COL32(255, 255, 255, 60), by.c_str());
}

void App::drawOverlays(float vx, float vy, float vw, float vh)
{
    const float s = m_dpiScale;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float pad = 10 * s;

    auto badge = [&](ImVec2 pos, const char* text, ImU32 fg, ImFont* font = nullptr) {
        ImFont* f = font ? font : ImGui::GetFont();
        const float fs = ImGui::GetFontSize();
        const ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0, text);
        dl->AddRectFilled(ImVec2(pos.x, pos.y), ImVec2(pos.x + ts.x + 12 * s, pos.y + ts.y + 6 * s), IM_COL32(0, 0, 0, 150), 5 * s);
        dl->AddText(f, fs, ImVec2(pos.x + 6 * s, pos.y + 3 * s), fg, text);
        return ts.x + 12 * s;
    };

    if (m_seq) {
        const ImagePtr cur = baseImage(m_cache.get(m_index));
        if (cur && !cur->valid()) {
            std::string msg = std::string(tr(S::LoadError)) + "  " + ToUtf8(GetFileName(m_seq->frames[m_index].path)) + "\n" + cur->error;
            const ImVec2 ts = ImGui::CalcTextSize(msg.c_str());
            dl->AddText(ImVec2(vx + (vw - ts.x) * 0.5f, vy + (vh - ts.y) * 0.5f), kError, msg.c_str());
        } else if (!m_shown) {
            const char* t = "\xE2\x80\xA6";
            const ImVec2 ts = ImGui::CalcTextSize(t);
            dl->AddText(ImVec2(vx + (vw - ts.x) * 0.5f, vy + (vh - ts.y) * 0.5f), kTextDim, t);
        }
    }

    // Top-left status badges (channel / exposure / bypass)
    float x = vx + pad;
    const float y = vy + pad;
    static const char* channelNames[] = { "RGB", "RED", "GREEN", "BLUE", "ALPHA", "LUMA" };
    if (m_channel != ChannelMode::RGB) x += badge(ImVec2(x, y), channelNames[(int)m_channel], kText) + 6 * s;
    if (m_exposure != 0.0f || m_gamma != 1.0f) {
        char buf[64];
        snprintf(buf, sizeof(buf), "EV %+.2f  \xCE\xB3 %.2f", m_exposure, m_gamma);
        x += badge(ImVec2(x, y), buf, kText) + 6 * s;
    }
    if (!m_colorManaged) x += badge(ImVec2(x, y), "OCIO OFF", IM_COL32(255, 180, 90, 255)) + 6 * s;
    if (m_seq && m_planProxy > 1) x += badge(ImVec2(x, y), m_planProxy == 2 ? "PROXY 1:2" : "PROXY 1:4", kText) + 6 * s;
    if (!m_uiVisible && m_seq) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%d", m_seq->frames[m_index].number);
        badge(ImVec2(x, y), buf, kTextDim, m_fontMono);
    }

    // Pixel inspector (bottom-left)
    if (m_hoverValid) {
        char buf[256];
        snprintf(buf, sizeof(buf), "%s%s%5d %5d   R %.4f  G %.4f  B %.4f  A %.4f", stackActive() ? m_hoverLayer.c_str() : "",
                 stackActive() ? "   " : "", m_hoverX, m_hoverY, m_hoverRGBA[0], m_hoverRGBA[1], m_hoverRGBA[2], m_hoverRGBA[3]);
        ImGui::PushFont(m_fontMono, 0.0f);
        const float fh = ImGui::GetFontSize();
        badge(ImVec2(vx + pad, vy + vh - pad - fh - 6 * s), buf, kText, m_fontMono);
        ImGui::PopFont();
    }

    // Toast
    if (!m_toast.empty() && ImGui::GetTime() >= 0) {
        LARGE_INTEGER f, t;
        QueryPerformanceFrequency(&f);
        QueryPerformanceCounter(&t);
        const double now = double(t.QuadPart) / double(f.QuadPart);
        if (now < m_toastUntil) {
            const float wrap = std::min(vw - 40 * s, 720 * s);
            const ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, wrap, m_toast.c_str());
            const ImVec2 p0(vx + (vw - ts.x) * 0.5f - 14 * s, vy + vh - ts.y - 44 * s);
            const ImVec2 p1(p0.x + ts.x + 28 * s, p0.y + ts.y + 16 * s);
            ImDrawList* fg = ImGui::GetForegroundDrawList();
            fg->AddRectFilled(p0, p1, IM_COL32(28, 28, 32, 240), 8 * s);
            fg->AddRect(p0, p1, m_toastError ? IM_COL32(255, 92, 92, 160) : IM_COL32(255, 255, 255, 30), 8 * s);
            fg->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(p0.x + 14 * s, p0.y + 8 * s),
                        m_toastError ? IM_COL32(255, 170, 170, 255) : kText, m_toast.c_str(), nullptr, wrap);
        } else {
            m_toast.clear();
        }
    }
}

// ---------------------------------------------------------------------------

void App::drawSettings()
{
    const float s = m_dpiScale;
    if (m_openSettings) {
        ImGui::OpenPopup("###settings");
        m_openSettings = false;
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(560 * s, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18 * s, 14 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10 * s);
    const std::string title = std::string(tr(S::Settings)) + "###settings";
    if (ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
        const float labelW = 170 * s;
        auto row = [&](const char* label) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            ImGui::SameLine(labelW);
            ImGui::SetNextItemWidth(-FLT_MIN);
        };

        // Language
        row(tr(S::Language));
        const char* langs[] = { tr(S::LangAuto), "English", "Italiano" };
        int li = m_settings.language == "en" ? 1 : m_settings.language == "it" ? 2 : 0;
        if (ImGui::Combo("##lang", &li, langs, 3)) {
            m_settings.language = li == 1 ? "en" : li == 2 ? "it" : "auto";
            applyLanguage();
        }

        // Default FPS
        row(tr(S::DefaultFps));
        double fps = m_settings.defaultFps;
        if (ImGui::InputDouble("##deffps", &fps, 1.0, 5.0, "%.3f"))
            m_settings.defaultFps = std::clamp(fps, 1.0, 240.0);

        // Cache
        row(tr(S::CacheMemory));
        const int maxMB = (int)std::max<uint64_t>(1024, GetPhysicalMemoryBytes() * 85 / 100 >> 20);
        bool autoCache = m_settings.cacheMB == 0;
        if (ImGui::Checkbox("Auto##cache", &autoCache)) {
            m_settings.cacheMB = autoCache ? 0 : 4096;
            applyCacheBudget();
        }
        if (!autoCache) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::SliderInt("##cachemb", &m_settings.cacheMB, 512, maxMB, "%d MB")) applyCacheBudget();
        }
        ImGui::SetCursorPosX(labelW);
        ImGui::TextDisabled("%.2f GB %s", m_cache.usedBytes() / 1073741824.0, tr(S::Cached));

        // Confirmations
        row(tr(S::Confirmations));
        ImGui::Checkbox(tr(S::ConfirmReplace), &m_settings.confirmReplace);

        // File associations
        ImGui::Spacing();
        ImGui::SeparatorText(tr(S::FileAssoc));
        const bool registered = AreFileAssociationsRegistered();
        ImGui::TextDisabled("%s", registered ? tr(S::Registered) : tr(S::NotRegistered));
        if (ImGui::Button(tr(S::Register))) {
            if (RegisterFileAssociations()) showToast(tr(S::Registered));
        }
        ImGui::SameLine();
        if (ImGui::Button(tr(S::DefaultApps))) {
            if (!registered) RegisterFileAssociations();
            OpenDefaultAppsSettings();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!registered);
        if (ImGui::Button(tr(S::Unregister))) UnregisterFileAssociations();
        ImGui::EndDisabled();

        drawUpdateSettings();

        // Shortcuts
        ImGui::Spacing();
        ImGui::SeparatorText(tr(S::Shortcuts));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextUnformatted(tr(S::ShortcutsText));
        ImGui::PopStyleColor();

        // About
        ImGui::Spacing();
        ImGui::SeparatorText(tr(S::About));
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s %s  \xC2\xB7  %s %s", APP_NAME, APP_VERSION, tr(S::CreatedBy), APP_PUBLISHER);
        ImGui::SameLine();
        if (ImGui::SmallButton(tr(S::About))) m_openAbout = true;

        ImGui::Spacing();
        const float bw = 110 * s;
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - bw - ImGui::GetStyle().WindowPadding.x);
        if (ImGui::Button(tr(S::Close), ImVec2(bw, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            m_settings.save();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);
}

// ---------------------------------------------------------------------------

void App::drawLutButton(float width)
{
    const float s = m_dpiScale;
    const bool on = !m_color.lutPath.empty();
    if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(kAccentDim));
    if (ImGui::Button(tr(S::Lut), ImVec2(width, 0))) ImGui::OpenPopup("LutMenu");
    if (on) ImGui::PopStyleColor();
    if (on) ImGui::SetItemTooltip("LUT: %s", m_color.lutPath.c_str());
    else ImGui::SetItemTooltip("%s", tr(S::NoLut));

    if (!ImGui::BeginPopup("LutMenu")) return;
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 340 * s);
    if (on) ImGui::TextUnformatted(ToUtf8(GetFileName(FromUtf8(m_color.lutPath))).c_str());
    else ImGui::TextDisabled("%s", tr(S::NoLut));
    ImGui::Separator();
    if (ImGui::MenuItem(tr(S::LoadLut))) defer([this] { loadLutDialog(); });
    if (ImGui::MenuItem(tr(S::RemoveLut), nullptr, false, on)) setLut("");

    ImGui::SeparatorText(tr(S::LutPosition));
    const bool grading = m_color.hasGradingSpace();
    if (ImGui::RadioButton(tr(S::LutDisplay), m_color.lutOnDisplay())) {
        m_color.lutPosition = LutPosition::Display;
        m_colorDirty = true;
    }
    ImGui::SetItemTooltip("%s", tr(S::LutDisplayHint));
    ImGui::BeginDisabled(!grading);
    if (ImGui::RadioButton(tr(S::LutGrading), !m_color.lutOnDisplay())) {
        m_color.lutPosition = LutPosition::Grading;
        m_colorDirty = true;
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("%s", grading ? tr(S::LutGradingHint) : tr(S::LutNoGrading));
    m_settings.lutPosition = (int)m_color.lutPosition;
    if (!m_colorManaged) ImGui::TextDisabled("%s", tr(S::LutRawHint));

    if (!m_settings.recentLuts.empty()) {
        ImGui::SeparatorText(tr(S::RecentLuts));
        std::string toLoad;
        for (const auto& path : m_settings.recentLuts) {
            ImGui::PushID(path.c_str());
            if (ImGui::MenuItem(ToUtf8(GetFileName(FromUtf8(path))).c_str(), nullptr, path == m_color.lutPath)) toLoad = path;
            ImGui::SetItemTooltip("%s", path.c_str());
            ImGui::PopID();
        }
        if (!toLoad.empty()) setLut(toLoad == m_color.lutPath ? std::string() : toLoad);   // the checked one toggles off
    }
    ImGui::PopTextWrapPos();
    ImGui::EndPopup();
}

void App::drawAlphaCombo(float width)
{
    const float s = m_dpiScale;
    ImagePtr own = stackActive() ? nullptr : m_shown;
    for (const StackLayer& l : m_stack)
        if (!l.seq && m_shownSet) { own = m_shownSet->find(l.key); break; }
    const AlphaMode mode = alphaModeFor(own);
    const std::string preview = std::string("\xCE\xB1 ") + (mode == AlphaMode::Premultiplied ? tr(S::AlphaPremult) : tr(S::AlphaStraight));
    ImGui::SetNextItemWidth(width);
    ImGui::SetNextWindowSizeConstraints(ImVec2(std::max(width, 180 * s), 0), ImVec2(FLT_MAX, FLT_MAX));
    if (ImGui::BeginCombo("##alpha", preview.c_str())) {
        if (ImGui::Selectable(tr(S::AlphaStraight), mode == AlphaMode::Straight)) m_alphaOverride = (int)AlphaMode::Straight;
        if (ImGui::Selectable(tr(S::AlphaPremult), mode == AlphaMode::Premultiplied)) m_alphaOverride = (int)AlphaMode::Premultiplied;
        ImGui::EndCombo();
    }
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28);
        ImGui::Text("%s: %s%s", tr(S::Alpha), mode == AlphaMode::Premultiplied ? tr(S::AlphaPremult) : tr(S::AlphaStraight),
                    m_alphaOverride < 0 ? (std::string("  (") + tr(S::AlphaDetected) + ")").c_str() : "");
        ImGui::TextDisabled("%s", tr(S::AlphaHint));
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void App::drawReplaceConfirm()
{
    const float s = m_dpiScale;
    if (m_openReplace) {
        ImGui::OpenPopup("###replace");
        m_openReplace = false;
        m_replaceDontAsk = false;
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(480 * s, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18 * s, 14 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10 * s);
    const std::string title = std::string(tr(S::ReplaceTitle)) + "###replace";
    if (ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextWrapped("%s", tr(S::ReplaceText));
        if (m_seq) ImGui::TextDisabled("%s", ToUtf8(m_seq->displayName()).c_str());
        ImGui::TextDisabled("\xE2\x86\x92 %s", ToUtf8(GetFileName(m_pendingOpen)).c_str());
        const std::vector<const char*> lost = workToLose();
        if (!lost.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.31f, 1), "%s", tr(S::ReplaceLoses));
            for (const char* w : lost) ImGui::BulletText("%s", w);
        }
        ImGui::Spacing();
        ImGui::Checkbox(tr(S::DontAskAgain), &m_replaceDontAsk);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        const float bw = 120 * s;
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 2 * bw - ImGui::GetStyle().ItemSpacing.x - ImGui::GetStyle().WindowPadding.x);
        if (ImGui::Button(tr(S::Cancel), ImVec2(bw, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            m_pendingOpen.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.29f, 0.56f, 1.0f, 0.75f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.29f, 0.56f, 1.0f, 0.9f));
        if (ImGui::Button(tr(S::OpenAnyway), ImVec2(bw, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
            if (m_replaceDontAsk) m_settings.confirmReplace = false;
            defer([this, path = std::move(m_pendingOpen)] { openPath(path); });
            m_pendingOpen.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor(2);
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);
}
