// Filters panel: denoise of the viewed sequence or of single stack layers (Open Image Denoise
// or OptiX, also temporal) and the one-time download of the Open Image Denoise libraries.
#include "App.h"
#include "I18n.h"
#include "Platform.h"

#include <windows.h>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdio>

namespace {

std::string Lower(std::string s)
{
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

void Label(const char* text)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", text);
}

void Hint(const char* text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

const ImVec4 kAccentButton(0.29f, 0.56f, 1.0f, 0.75f);
const ImVec4 kAccentButtonHover(0.29f, 0.56f, 1.0f, 0.9f);
const ImVec4 kWarn(1.0f, 0.78f, 0.31f, 1.0f);
const ImVec4 kErr(1.0f, 0.36f, 0.36f, 1.0f);

// A layer that can serve as this guide: 3 channels (RGB / XYZ), 2 are enough for motion vectors.
bool GuideCandidate(const ExrLayer& l, GuideKind kind)
{
    return !l.isRootColor && l.channels.size() >= (kind == GuideKind::Flow ? 2u : 3u);
}

}  // namespace

// ---------------------------------------------------------------------------
// State

int App::denoiseGuide(GuideKind kind, const std::vector<ExrLayer>& layers) const
{
    if (kind == GuideKind::Normal && denoiseGuide(GuideKind::Albedo, layers) < 0) return -1;   // a normal guide needs albedo
    const std::string& choice = kind == GuideKind::Albedo ? m_guideAlbedo : kind == GuideKind::Normal ? m_guideNormal : m_guideFlow;
    if (choice == "-" || layers.size() < 2) return -1;
    if (!choice.empty()) {   // a layer picked by name; a file without it has no guide
        for (size_t i = 0; i < layers.size(); ++i)
            if (layers[i].label == choice) return (int)i;
        return -1;
    }
    // Automatic: the renderer's denoising passes first (Cycles "Denoising Albedo"), then any match.
    int best = -1, bestScore = 0;
    for (size_t i = 0; i < layers.size(); ++i) {
        if (!GuideCandidate(layers[i], kind)) continue;
        const std::string l = Lower(layers[i].label);
        int score = 0;
        if (kind == GuideKind::Albedo && l.find("albedo") != std::string::npos) score = 1;
        if (kind == GuideKind::Normal && (l.find("normal") != std::string::npos || l == "n")) score = 1;
        if (kind == GuideKind::Flow && (l.find("vector") != std::string::npos || l.find("motion") != std::string::npos ||
                                        l.find("velocity") != std::string::npos || l == "mv"))
            score = 1;
        if (score && l.find("denois") != std::string::npos) score = 2;
        if (score > bestScore) { best = (int)i; bestScore = score; }
    }
    return best;
}

DenoiseSpecPtr App::makeDenoiseSpec(const std::vector<ExrLayer>& layers) const
{
    auto d = std::make_shared<DenoiseSpec>();
    d->engine = (DenoiseEngine)m_settings.denoiseEngine;
    d->device = (OidnDevice)m_settings.oidnDevice;
    d->quality = (OidnQuality)m_settings.oidnQuality;
    d->temporal = d->engine == DenoiseEngine::Optix && m_settings.optixTemporal;
    d->key = d->engine == DenoiseEngine::Optix ? (d->temporal ? "dn:optixT" : "dn:optix")
                                               : "dn:oidn" + std::to_string(m_settings.oidnDevice) + std::to_string(m_settings.oidnQuality);
    auto guide = [&](int index, const char* tag) -> LoadOptionsPtr {
        if (index < 0) return nullptr;
        const ExrLayer& l = layers[index];
        auto o = std::make_shared<LoadOptions>();
        o->part = l.part;
        o->channels = l.channels;
        d->key += std::string("|") + tag + std::to_string(l.part);
        for (const auto& c : l.channels) d->key += "," + c;
        return o;
    };
    d->albedo = guide(denoiseGuide(GuideKind::Albedo, layers), "a");
    // Temporal OptiX wants camera-space normals, which renderers rarely write: no normal guide there.
    if (!d->temporal) d->normal = guide(denoiseGuide(GuideKind::Normal, layers), "n");
    if (d->temporal) {
        d->flow = guide(denoiseGuide(GuideKind::Flow, layers), "f");
        d->flowInvert = m_settings.flowInvert;
        d->flowFlipY = m_settings.flowFlipY;
        if (d->flow) d->key += std::string("|") + (d->flowInvert ? "i" : "") + (d->flowFlipY ? "y" : "");
    }
    return d;
}

DenoiseSpecPtr App::denoiseSpec() const
{
    return denoiseActive() ? makeDenoiseSpec(m_exrInfo.layers) : nullptr;
}

bool App::denoiseInUse() const
{
    if (!stackActive()) return m_denoise;
    return std::any_of(m_stack.begin(), m_stack.end(), [](const StackLayer& l) { return l.denoise; });
}

bool App::requireDenoiser(std::function<void()> then)
{
    if ((DenoiseEngine)m_settings.denoiseEngine != DenoiseEngine::Oidn || OidnInstalled()) return true;
    m_afterOidnInstall = std::move(then);   // explicit consent first: the dialog runs it once installed
    m_openDenoiseDownload = true;
    return false;
}

void App::denoiseUnused()
{
    if (!denoiseInUse()) ReleaseDenoisers();
}

void App::setDenoise(bool on)
{
    if (on == m_denoise) return;
    if (on && !requireDenoiser([this] { setDenoise(true); })) return;
    m_denoise = on;
    applyLoadPlan();
    denoiseUnused();
}

void App::stackSetDenoise(int index, bool on)
{
    if (index < 0 || index >= (int)m_stack.size() || m_stack[index].denoise == on) return;
    const int id = m_stack[index].id;
    if (on && !requireDenoiser([this, id] {
            for (int i = 0; i < (int)m_stack.size(); ++i)
                if (m_stack[i].id == id) stackSetDenoise(i, true);
        }))
        return;
    m_stack[index].denoise = on;
    applyLoadPlan();
    denoiseUnused();
}

void App::denoiseSettingsChanged()
{
    if (!denoiseInUse()) return;
    if (!requireDenoiser([this] { applyLoadPlan(); })) {
        // Switched to Open Image Denoise before downloading it: nothing is denoised meanwhile.
        m_denoise = false;
        for (StackLayer& l : m_stack) l.denoise = false;
        applyLoadPlan();
        return;
    }
    applyLoadPlan();
}

ImagePtr App::stackImage(const FrameSetPtr& set, const StackLayer& s) const
{
    if (!set) return nullptr;
    if (!s.loadKey.empty() && s.loadKey != s.key)
        if (ImagePtr img = set->find(s.loadKey)) return img;
    return set->find(s.key);   // until its filtered frame is ready
}

// ---------------------------------------------------------------------------
// Panel

void App::drawGuideCombo(const char* id, GuideKind kind, float width)
{
    std::string& choice = kind == GuideKind::Albedo ? m_guideAlbedo : kind == GuideKind::Normal ? m_guideNormal : m_guideFlow;
    const auto& layers = m_exrInfo.layers;
    const int current = denoiseGuide(kind, layers);
    std::string preview;
    if (choice == "-") preview = tr(S::GuideOff);
    else if (choice.empty()) preview = std::string(tr(S::GuideAuto)) + (current >= 0 ? " \xC2\xB7 " + layers[current].label : "");
    else preview = current >= 0 ? layers[current].label : choice + " (?)";
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo(id, preview.c_str(), ImGuiComboFlags_HeightLarge)) {
        std::string pick = choice;
        if (ImGui::Selectable(tr(S::GuideAuto), choice.empty())) pick.clear();
        if (ImGui::Selectable(tr(S::GuideOff), choice == "-")) pick = "-";
        ImGui::Separator();
        for (const ExrLayer& l : layers) {
            if (!GuideCandidate(l, kind)) continue;
            if (ImGui::Selectable(l.label.c_str(), !choice.empty() && choice == l.label)) pick = l.label;
        }
        ImGui::EndCombo();
        if (pick != choice) {
            choice = pick;
            denoiseSettingsChanged();
        }
    }
}

void App::drawFilterPanel(float x, float y, float w, float h)
{
    const float s = m_dpiScale;
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14 * s, 12 * s));
    ImGui::Begin("##filters", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                        ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar();

    ImGui::TextUnformatted(tr(S::Filters));
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetStyle().WindowPadding.x - ImGui::GetFrameHeight() * 0.6f);
    if (ImGui::SmallButton("x")) m_filterPanel = false;
    ImGui::Separator();
    ImGui::Spacing();

    // --- Denoise ---
    const DenoiseEngine engine = (DenoiseEngine)m_settings.denoiseEngine;
    const bool oidnReady = OidnInstalled();
    const bool optixUsable = OptixBuiltIn() && NvidiaDriverPresent();
    if (stackActive()) {
        // In the stack each layer has its own switch; the engine below is shared.
        ImGui::TextUnformatted(tr(S::Denoise));
        Hint(tr(S::DenoiseStackHint));
    } else {
        bool on = m_denoise;
        if (ImGui::Checkbox(tr(S::Denoise), &on)) setDenoise(on);
        Hint(tr(S::DenoiseHint));
    }
    ImGui::Spacing();

    const float fieldW = ImGui::GetContentRegionAvail().x;
    Label(tr(S::DenoiseEngine));
    auto setEngine = [&](DenoiseEngine e) {
        m_settings.denoiseEngine = (int)e;
        if (denoiseInUse()) ReleaseDenoisers();
        denoiseSettingsChanged();
    };
    if (ImGui::RadioButton(tr(S::EngineOidn), engine == DenoiseEngine::Oidn) && engine != DenoiseEngine::Oidn) setEngine(DenoiseEngine::Oidn);
    ImGui::BeginDisabled(!optixUsable && engine != DenoiseEngine::Optix);
    if (ImGui::RadioButton(tr(S::EngineOptix), engine == DenoiseEngine::Optix) && engine != DenoiseEngine::Optix) setEngine(DenoiseEngine::Optix);
    ImGui::EndDisabled();
    ImGui::Spacing();

    if (engine == DenoiseEngine::Oidn) {
        const char* devices[] = { tr(S::DeviceAuto), tr(S::DeviceCpu), tr(S::DeviceGpu) };
        const char* qualities[] = { tr(S::DnQualityHigh), tr(S::DnQualityBalanced), tr(S::DnQualityFast) };
        Label(tr(S::DenoiseDevice));
        ImGui::SetNextItemWidth(fieldW);
        if (ImGui::Combo("##oidndevice", &m_settings.oidnDevice, devices, 3)) {
            if (denoiseInUse()) ReleaseDenoisers();
            denoiseSettingsChanged();
        }
        Label(tr(S::Quality));
        ImGui::SetNextItemWidth(fieldW);
        if (ImGui::Combo("##oidnquality", &m_settings.oidnQuality, qualities, 3)) denoiseSettingsChanged();
        ImGui::Spacing();
        if (!oidnReady) {
            ImGui::PushStyleColor(ImGuiCol_Text, kWarn);
            ImGui::TextWrapped("%s", tr(S::OidnNotInstalled));
            ImGui::PopStyleColor();
            if (ImGui::Button(tr(S::DownloadOidn))) {
                m_afterOidnInstall = nullptr;
                m_openDenoiseDownload = true;
            }
        } else {
            ImGui::TextDisabled("Open Image Denoise %s \xC2\xB7 %s", kOidnVersion, tr(S::InstalledIn));
            Hint(ToUtf8(OidnDir()).c_str());
            if (!OidnLoaded()) {
                if (ImGui::SmallButton(tr(S::RemoveLibraries))) {
                    m_oidnRemoveError.clear();
                    if (!RemoveOidn(m_oidnRemoveError)) Log("denoise: remove failed: %s", m_oidnRemoveError.c_str());
                }
                ImGui::SetItemTooltip("%s", tr(S::RemoveLibrariesHint));
            }
            if (!m_oidnRemoveError.empty()) ImGui::TextColored(kErr, "%s", m_oidnRemoveError.c_str());
        }
    } else {
        Hint(tr(S::OptixInfo));
        if (!OptixBuiltIn()) ImGui::TextColored(kWarn, "%s", tr(S::OptixNotBuilt));
        else if (!NvidiaDriverPresent()) ImGui::TextColored(kWarn, "%s", tr(S::OptixNoDriver));
        ImGui::Spacing();
        if (ImGui::Checkbox(tr(S::OptixTemporal), &m_settings.optixTemporal)) denoiseSettingsChanged();
        Hint(tr(S::OptixTemporalHint));
    }

    // Guides
    const bool temporal = engine == DenoiseEngine::Optix && m_settings.optixTemporal;
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextUnformatted(tr(S::DenoiseGuides));
    if (hasLayers()) {
        const float labelW = std::max({ ImGui::CalcTextSize(tr(S::GuideAlbedo)).x, ImGui::CalcTextSize(tr(S::GuideNormal)).x,
                                        ImGui::CalcTextSize(tr(S::GuideMotion)).x }) + 10 * s;
        auto guideRow = [&](const char* label, const char* id, GuideKind kind, bool enabled) {
            Label(label);
            ImGui::SameLine(labelW + ImGui::GetStyle().WindowPadding.x);
            ImGui::BeginDisabled(!enabled);
            drawGuideCombo(id, kind, ImGui::GetContentRegionAvail().x);
            ImGui::EndDisabled();
        };
        guideRow(tr(S::GuideAlbedo), "##galbedo", GuideKind::Albedo, true);
        if (!temporal) guideRow(tr(S::GuideNormal), "##gnormal", GuideKind::Normal, denoiseGuide(GuideKind::Albedo, m_exrInfo.layers) >= 0);
        if (temporal) {
            guideRow(tr(S::GuideMotion), "##gflow", GuideKind::Flow, true);
            if (denoiseGuide(GuideKind::Flow, m_exrInfo.layers) >= 0) {
                ImGui::SetCursorPosX(labelW + ImGui::GetStyle().WindowPadding.x);
                if (ImGui::Checkbox(tr(S::FlowInvert), &m_settings.flowInvert)) denoiseSettingsChanged();
                ImGui::SameLine();
                if (ImGui::Checkbox(tr(S::FlowFlipY), &m_settings.flowFlipY)) denoiseSettingsChanged();
            }
            Hint(tr(S::GuidesTemporalHint));
        } else {
            Hint(tr(S::GuidesHint));
        }
    } else {
        Hint(tr(S::GuidesNoLayers));
    }

    // Status
    if (denoiseInUse()) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        const DenoiseStatus st = GetDenoiseStatus();
        if (!st.device.empty()) {
            Label(tr(S::DenoiseDevice));
            ImGui::SameLine();
            ImGui::TextUnformatted(st.device.c_str());
        }
        if (st.frames > 0) ImGui::TextDisabled("%.0f %s", st.lastMs, tr(S::MsPerFrame));
        if (st.busy) ImGui::TextColored(kWarn, "%s", tr(S::DenoiseBusy));
        if (!st.error.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, kErr);
            ImGui::TextWrapped("%s", st.error.c_str());
            ImGui::PopStyleColor();
        }
        if (!temporal) {
            ImGui::Spacing();
            Hint(tr(S::DenoiseFlicker));
        }
    }
    ImGui::End();
}

// ---------------------------------------------------------------------------
// One-time download of Open Image Denoise: says what, from where and where it goes, before anything starts.

void App::drawDenoiseDownload()
{
    const float s = m_dpiScale;
    if (m_openDenoiseDownload) {
        ImGui::OpenPopup("###oidndownload");
        m_openDenoiseDownload = false;
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(500 * s, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18 * s, 14 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10 * s);
    const std::string title = std::string(tr(S::DownloadRequired)) + " \xC2\xB7 Open Image Denoise###oidndownload";
    if (ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
        const std::shared_ptr<OidnInstall> job = m_oidnInstall;
        const bool running = job && !job->done;
        const bool failed = job && job->done && !job->ok;

        // Finished: enable the filter and close.
        if (job && job->done && job->ok) {
            m_oidnInstall.reset();
            m_settings.denoiseEngine = (int)DenoiseEngine::Oidn;
            showToast(tr(S::OidnInstalled));
            if (auto then = std::move(m_afterOidnInstall)) {   // what asked for it: filter on, batch start
                m_afterOidnInstall = nullptr;
                then();
            }
            ImGui::CloseCurrentPopup();
        }

        ImGui::TextWrapped("%s", tr(S::OidnNotInstalled));
        ImGui::Spacing();
        char info[512];
        snprintf(info, sizeof(info), tr(S::OidnDownloadInfo), kOidnVersion, kOidnDownloadMB, kOidnInstalledMB);
        ImGui::TextWrapped("%s", info);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", ToUtf8(OidnDir()).c_str());
        ImGui::PopStyleColor();
        ImGui::Spacing();
        ImGui::TextWrapped("%s", tr(S::OidnDownloadInfo2));
        ImGui::Spacing();

        if (running) {
            const int stage = job->stage;
            const uint64_t got = job->received, total = job->total;
            char overlay[96];
            float frac = 0.0f;
            if (stage == 0) {
                frac = total ? float(double(got) / double(total)) : 0.0f;
                if (total) snprintf(overlay, sizeof(overlay), "%s  %.1f / %.1f MB", tr(S::Downloading), got / 1048576.0, total / 1048576.0);
                else snprintf(overlay, sizeof(overlay), "%s  %.1f MB", tr(S::Downloading), got / 1048576.0);
            } else {
                frac = 1.0f;
                snprintf(overlay, sizeof(overlay), "%s", stage == 1 ? tr(S::Verifying) : tr(S::Extracting));
            }
            ImGui::ProgressBar(frac, ImVec2(-FLT_MIN, 0), overlay);
        } else if (failed) {
            std::lock_guard lock(job->mutex);
            ImGui::PushStyleColor(ImGuiCol_Text, kErr);
            ImGui::TextWrapped("%s: %s", tr(S::DownloadFailed), job->error.c_str());
            ImGui::PopStyleColor();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        const float bw = 150 * s;
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 2 * bw - ImGui::GetStyle().ItemSpacing.x - ImGui::GetStyle().WindowPadding.x);
        if (ImGui::Button(running ? tr(S::Cancel) : tr(S::Close), ImVec2(bw, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            if (job) job->cancel = true;   // the worker cleans up its temporary folder
            m_oidnInstall.reset();
            m_afterOidnInstall = nullptr;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(running);
        ImGui::PushStyleColor(ImGuiCol_Button, kAccentButton);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccentButtonHover);
        if (ImGui::Button(failed ? tr(S::Retry) : tr(S::DownloadAndEnable), ImVec2(bw, 0))) {
            const HWND hwnd = m_hwnd;
            m_oidnInstall = StartOidnInstall([hwnd] { PostMessageW(hwnd, WM_NULL, 0, 0); });
        }
        ImGui::PopStyleColor(2);
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);
}
