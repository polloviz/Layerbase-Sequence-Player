// Optional update check: one HTTPS request for a small JSON file, at most once a
// day, off the UI thread. A newer version shows a dismissible banner.
#include "App.h"
#include "I18n.h"
#include "Platform.h"
#include "UpdateCheck.h"
#include "Version.h"

#include <windows.h>
#include <imgui.h>

#include <atomic>
#include <cfloat>
#include <ctime>
#include <thread>

struct App::UpdateCheckState {
    std::atomic<bool> done{ false };
    bool ok = false;
    bool manual = false;
    UpdateInfo info;
};

void App::startUpdateCheck(bool manual)
{
    if (m_updateCheck && !m_updateCheck->done) return;   // already running
    auto st = std::make_shared<UpdateCheckState>();
    st->manual = manual;
    m_updateCheck = st;
    // Detached: the state is shared, and the request times out after a few seconds.
    std::thread([st, hwnd = m_hwnd] {
        if (!st->manual) Sleep(3000);   // keep startup I/O free for the first frames
        st->ok = FetchUpdateInfo(st->info);
        st->done = true;
        PostMessageW(hwnd, WM_NULL, 0, 0);   // wake the idle message loop
    }).detach();
}

void App::pollUpdateCheck()
{
    if (!m_updateCheck || !m_updateCheck->done) return;
    const auto st = std::move(m_updateCheck);
    m_updateCheck.reset();
    if (!st->ok) {
        if (st->manual) showToast(tr(S::UpdateFailed), true);
        return;
    }
    m_settings.lastUpdateCheck = (long long)std::time(nullptr);
    const UpdateInfo& u = st->info;
    const bool newer = CompareVersions(u.version, APP_VERSION) > 0;
    if (newer && (st->manual || u.version != m_settings.skipVersion)) {
        m_updateVersion = u.version;
        m_updateUrl = u.url;
        m_updateNotes = GetLanguage() == Lang::Italian && !u.notesIt.empty() ? u.notesIt : u.notesEn;
    } else if (st->manual) {
        showToast(newer ? tr(S::UpdateAvailable) : tr(S::UpToDate));
    }
    m_renderFrames = 3;
}

void App::drawUpdateBanner()
{
    if (m_updateVersion.empty()) return;
    const float s = m_dpiScale;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float w = 380 * s, margin = 14 * s;
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x - m_panelW - margin, vp->Pos.y + vp->Size.y - m_bottomBarH - margin),
                            ImGuiCond_Always, ImVec2(1, 1));
    ImGui::SetNextWindowSize(ImVec2(w, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16 * s, 12 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10 * s);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.13f, 0.15f, 0.2f, 0.97f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.29f, 0.56f, 1.0f, 0.6f));
    ImGui::Begin("##update", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                          ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::Text("%s: %s %s", tr(S::UpdateAvailable), APP_NAME, m_updateVersion.c_str());
    ImGui::TextDisabled("%s %s", tr(S::InstalledVersion), APP_VERSION);
    if (!m_updateNotes.empty()) {
        ImGui::Spacing();
        ImGui::PushTextWrapPos(w - 16 * s);
        ImGui::TextUnformatted(m_updateNotes.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.29f, 0.56f, 1.0f, 0.75f));
    if (ImGui::Button(tr(S::Download))) {
        OpenUrl(m_updateUrl.empty() ? APP_PRODUCT_URL_W : FromUtf8(m_updateUrl).c_str());
        m_updateVersion.clear();
    }
    ImGui::PopStyleColor();
    ImGui::SameLine();
    if (ImGui::Button(tr(S::Later))) m_updateVersion.clear();
    ImGui::SameLine();
    if (ImGui::Button(tr(S::SkipVersion))) {
        m_settings.skipVersion = m_updateVersion;
        m_updateVersion.clear();
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
}

void App::drawUpdateSettings()
{
    ImGui::Spacing();
    ImGui::SeparatorText(tr(S::Updates));
    ImGui::Checkbox(tr(S::CheckUpdates), &m_settings.checkUpdates);
    ImGui::SameLine();
    const bool running = m_updateCheck != nullptr;
    ImGui::BeginDisabled(running);
    if (ImGui::SmallButton(running ? "..." : tr(S::CheckNow))) startUpdateCheck(true);
    ImGui::EndDisabled();
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("%s", tr(S::UpdatePrivacy));
    if (IsPortable()) ImGui::TextDisabled("%s %s", tr(S::PortableMode), ToUtf8(GetAppDataDir()).c_str());
    ImGui::PopTextWrapPos();
}
