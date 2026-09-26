// About dialog: logo, credits (Layerbase Luxury Vision), license and third-party notices.
#include <GL/glew.h>
#include "App.h"
#include "I18n.h"
#include "Platform.h"
#include "Version.h"

#include <imgui.h>
#include <stb_image.h>

#include <cfloat>

static unsigned LoadLogoTexture()
{
    const std::string png = LoadResourceData(101);
    if (png.empty()) return 0;
    int w = 0, h = 0, n = 0;
    stbi_uc* px = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(png.data()), (int)png.size(), &w, &h, &n, 4);
    if (!px) return 0;
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glGenerateMipmap(GL_TEXTURE_2D);
    stbi_image_free(px);
    return tex;
}

void App::drawAbout()
{
    const float s = m_dpiScale;
    if (m_openAbout) {
        ImGui::OpenPopup("###about");
        m_openAbout = false;
        m_aboutPage = 0;
        if (!m_logoTex) m_logoTex = LoadLogoTexture();
        auto load = [](int id) {
            std::string t = LoadResourceData(id);
            if (t.rfind("\xEF\xBB\xBF", 0) == 0) t.erase(0, 3);   // UTF-8 BOM
            return t;
        };
        if (m_licenseText.empty()) m_licenseText = load(GetLanguage() == Lang::Italian ? 104 : 103);
        if (m_noticesText.empty()) m_noticesText = load(102);
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(560 * s, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22 * s, 18 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12 * s);
    const std::string title = std::string(tr(S::AboutTitle)) + "###about";
    if (ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
        if (m_aboutPage == 0) {
            const float logo = 112 * s;
            if (m_logoTex) {
                ImGui::Image(ImTextureRef((ImTextureID)(intptr_t)m_logoTex), ImVec2(logo, logo));
                ImGui::SameLine(0, 18 * s);
            }
            ImGui::BeginGroup();
            ImGui::Dummy(ImVec2(0, 8 * s));
            ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.7f);
            ImGui::TextUnformatted(APP_NAME);
            ImGui::PopFont();
            ImGui::TextDisabled("%s %s", "Version", APP_VERSION);
            ImGui::Spacing();
            ImGui::Text("%s", tr(S::CreatedBy));
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.72f, 1.0f, 1));
            ImGui::TextUnformatted(APP_PUBLISHER);
            ImGui::PopStyleColor();
            if (ImGui::TextLink("layerbase.it")) OpenUrl(APP_WEBSITE_W);
            ImGui::EndGroup();

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::PushTextWrapPos(0);
            ImGui::TextUnformatted(tr(S::AboutText));
            ImGui::TextDisabled("%s", tr(S::FreewareText));
            ImGui::PopTextWrapPos();
            ImGui::Spacing();
            ImGui::TextDisabled("OpenColorIO %s  \xC2\xB7  OpenEXR  \xC2\xB7  Dear ImGui %s", OCIO::GetVersion(), IMGUI_VERSION);
            ImGui::TextDisabled("\xC2\xA9 2026 %s", APP_PUBLISHER);
        } else {
            const std::string& text = m_aboutPage == 1 ? m_licenseText : m_noticesText;
            ImGui::TextUnformatted(m_aboutPage == 1 ? tr(S::License) : tr(S::ThirdParty));
            ImGui::Spacing();
            ImGui::BeginChild("##text", ImVec2(0, 380 * s), ImGuiChildFlags_Borders);
            ImGui::PushFont(m_fontMono, 0.0f);
            ImGui::TextUnformatted(text.data(), text.data() + text.size());
            ImGui::PopFont();
            ImGui::EndChild();
        }

        ImGui::Spacing();
        ImGui::Spacing();
        auto tab = [&](const char* label, int page) {
            const bool active = m_aboutPage == page;
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.29f, 0.56f, 1.0f, 0.35f));
            if (ImGui::Button(label)) m_aboutPage = page;
            if (active) ImGui::PopStyleColor();
            ImGui::SameLine();
        };
        tab(tr(S::Info), 0);
        tab(tr(S::License), 1);
        tab(tr(S::ThirdParty), 2);
        const float bw = 110 * s;
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - bw - ImGui::GetStyle().WindowPadding.x);
        if (ImGui::Button(tr(S::Close), ImVec2(bw, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);
}
