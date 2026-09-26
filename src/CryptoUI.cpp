// EXR layer selection and the Cryptomatte panel.
#include "App.h"
#include "I18n.h"
#include "Platform.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cstdio>

std::string App::currentLayerLabel() const
{
    if (m_layer < 0 || m_layer >= (int)m_exrInfo.layers.size()) return {};
    return m_exrInfo.layers[m_layer].label;
}

void App::applyLoadOptions()
{
    auto o = std::make_shared<LoadOptions>();
    if (m_layer >= 0 && m_layer < (int)m_exrInfo.layers.size() && !m_exrInfo.layers[m_layer].isRootColor) {
        o->part = m_exrInfo.layers[m_layer].part;
        o->channels = m_exrInfo.layers[m_layer].channels;
    }
    if (m_matte != MatteMode::Off && hasCrypto()) {
        const auto& layers = cryptoLayers();
        const CryptoLayer& c = layers[std::clamp(m_crypto, 0, (int)layers.size() - 1)];
        o->cryptoPart = c.part;
        o->cryptoChannels = c.channels;
        o->selection = m_cryptoSel;
        o->cryptoColors = m_matte == MatteMode::Ids;
        if (m_matteSeq) o->cryptoFiles = m_matteFiles;
    }
    m_loadOpts = o;
    m_cache.setLoadOptions(o);
    if (!o->cryptoActive()) m_viewer.setMatteMode(MatteMode::Off);
}

void App::setLayer(int index)
{
    if (index == m_layer) return;
    m_layer = index;
    applyLoadOptions();
}

void App::setMatteMode(MatteMode m)
{
    if (m == m_matte) return;
    m_matte = m;
    applyLoadOptions();
}

void App::toggleCryptoId(uint32_t id)
{
    auto it = std::lower_bound(m_cryptoSel.begin(), m_cryptoSel.end(), id);
    if (it != m_cryptoSel.end() && *it == id) m_cryptoSel.erase(it);
    else m_cryptoSel.insert(it, id);
    if (m_matte == MatteMode::Off) m_matte = MatteMode::Overlay;
    applyLoadOptions();
}

void App::pickCrypto()
{
    if (!m_seq || !m_shown || !m_loadOpts || !m_loadOpts->cryptoActive()) return;
    std::wstring file = m_seq->frames[m_index].path;
    if (m_loadOpts->cryptoFiles) file = m_index < (int)m_loadOpts->cryptoFiles->size() ? (*m_loadOpts->cryptoFiles)[m_index] : L"";
    uint32_t id = 0;
    float cov = 0;
    if (!PickCryptoId(file, *m_loadOpts, m_hoverX, m_hoverY, m_shown->width, m_shown->height, id, cov)) {
        m_lastPick = "-";
        return;
    }
    const CryptoLayer& c = cryptoLayers()[m_crypto];
    char buf[64];
    snprintf(buf, sizeof(buf), "  (%.0f%%)", cov * 100.0f);
    m_lastPick = c.nameOf(id) + buf;
    toggleCryptoId(id);
}

void App::drawLayerCombo(float width)
{
    ImGui::SetNextItemWidth(width);
    ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0), ImVec2(FLT_MAX, 460 * m_dpiScale));
    const std::string preview = currentLayerLabel();
    if (ImGui::BeginCombo("##layer", preview.c_str(), ImGuiComboFlags_HeightLargest)) {
        for (int i = 0; i < (int)m_exrInfo.layers.size(); ++i) {
            const ExrLayer& l = m_exrInfo.layers[i];
            if (ImGui::Selectable(l.label.c_str(), i == m_layer)) setLayer(i);
            if (i == m_layer && ImGui::IsWindowAppearing()) ImGui::SetScrollHereY();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                std::string ch;
                for (auto& c : l.channels) ch += (ch.empty() ? "" : ", ") + c;
                ImGui::SetTooltip("%s", ch.c_str());
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("%s", tr(S::Layer));
}

void App::drawCryptoPanel(float x, float y, float w, float h)
{
    const float s = m_dpiScale;
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14 * s, 12 * s));
    ImGui::Begin("##crypto", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar();

    ImGui::TextUnformatted("Cryptomatte");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetStyle().WindowPadding.x - ImGui::GetFrameHeight() * 0.6f);
    if (ImGui::SmallButton("x")) m_cryptoPanel = false;
    ImGui::Spacing();

    // Source: the sequence itself or an external Cryptomatte sequence
    if (m_matteSeq) {
        ImGui::TextDisabled("%s", tr(S::MatteSource));
        ImGui::TextUnformatted(ToUtf8(m_matteSeq->displayName()).c_str());
        ImGui::SetItemTooltip("%s", ToUtf8(m_matteSeq->directory).c_str());
        if (m_matteMissing > 0) ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.35f, 1), "%d %s", m_matteMissing, tr(S::MatteMissing));
        if (ImGui::Button(tr(S::LoadMatte))) defer([this] { loadMatteDialog(); });
        ImGui::SameLine();
        if (ImGui::Button(tr(S::RemoveMatte))) clearMatteSequence();
    } else {
        if (hasCrypto()) ImGui::TextDisabled("%s %s", tr(S::MatteSource), tr(S::MatteOwn));
        else {
            ImGui::PushTextWrapPos(0);
            ImGui::TextDisabled("%s", tr(S::NoCryptoHere));
            ImGui::PopTextWrapPos();
        }
        if (ImGui::Button(tr(S::LoadMatte), ImVec2(-FLT_MIN, 0))) defer([this] { loadMatteDialog(); });
    }
    if (!hasCrypto()) {
        ImGui::End();
        return;
    }
    ImGui::Spacing();

    // Layer
    const auto& layers = cryptoLayers();
    const CryptoLayer& cl = layers[std::clamp(m_crypto, 0, (int)layers.size() - 1)];
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##cryptolayer", cl.name.c_str())) {
        for (int i = 0; i < (int)layers.size(); ++i)
            if (ImGui::Selectable(layers[i].name.c_str(), i == m_crypto) && i != m_crypto) {
                m_crypto = i;
                m_cryptoSel.clear();
                applyLoadOptions();
            }
        ImGui::EndCombo();
    }

    // Mode
    const char* modes[] = { tr(S::MatteOff), tr(S::MatteIds), tr(S::MatteOverlay), tr(S::MatteMasked), tr(S::MatteMatte) };
    const float bw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2) / 3.0f;
    for (int i = 0; i < 5; ++i) {
        const bool active = (int)m_matte == i;
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.29f, 0.56f, 1.0f, 0.45f));
        if (ImGui::Button(modes[i], ImVec2(bw, 0))) setMatteMode((MatteMode)i);
        if (active) ImGui::PopStyleColor();
        if (i % 3 != 2 && i != 4) ImGui::SameLine();
    }
    ImGui::Spacing();
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("%s", tr(S::CryptoHint));
    ImGui::PopTextWrapPos();
    if (!m_lastPick.empty()) ImGui::TextDisabled("%s: %s", tr(S::LastPick), m_lastPick.c_str());

    auto swatch = [&](uint32_t id) {
        float c[3];
        CryptoIdColor(id, c);
        ImGui::ColorButton("##sw", ImVec4(c[0], c[1], c[2], 1), ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder,
                           ImVec2(ImGui::GetTextLineHeight(), ImGui::GetTextLineHeight()));
        ImGui::SameLine();
    };

    // Selection
    ImGui::Spacing();
    char title[64];
    snprintf(title, sizeof(title), "%s (%d)", tr(S::Selected), (int)m_cryptoSel.size());
    ImGui::SeparatorText(title);
    if (!m_cryptoSel.empty()) {
        if (ImGui::SmallButton(tr(S::ClearSelection))) {
            m_cryptoSel.clear();
            applyLoadOptions();
        }
        ImGui::BeginChild("##sel", ImVec2(0, std::min(150 * s, (ImGui::GetTextLineHeightWithSpacing() + 4 * s) * m_cryptoSel.size() + 8 * s)));
        uint32_t remove = 0;
        bool doRemove = false;
        for (uint32_t id : m_cryptoSel) {
            ImGui::PushID((int)id);
            if (ImGui::SmallButton("x")) { remove = id; doRemove = true; }
            ImGui::SameLine();
            swatch(id);
            ImGui::TextUnformatted(cl.nameOf(id).c_str());
            ImGui::PopID();
        }
        ImGui::EndChild();
        if (doRemove) toggleCryptoId(remove);
    }

    // Manifest
    ImGui::Spacing();
    snprintf(title, sizeof(title), "%s (%d)", tr(S::Objects), (int)cl.manifest.size());
    ImGui::SeparatorText(title);
    if (cl.manifest.empty()) {
        ImGui::TextDisabled("%s", tr(S::NoManifest));
    } else {
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##cfilter", tr(S::Search), m_cryptoFilter, sizeof(m_cryptoFilter));
        std::vector<int> visible;
        visible.reserve(cl.manifest.size());
        std::string needle = m_cryptoFilter;
        for (auto& c : needle) c = (char)tolower((unsigned char)c);
        for (int i = 0; i < (int)cl.manifest.size(); ++i) {
            if (!needle.empty()) {
                std::string n = cl.manifest[i].first;
                for (auto& c : n) c = (char)tolower((unsigned char)c);
                if (n.find(needle) == std::string::npos) continue;
            }
            visible.push_back(i);
        }
        ImGui::BeginChild("##manifest", ImVec2(0, 0), ImGuiChildFlags_None);
        ImGuiListClipper clipper;
        clipper.Begin((int)visible.size());
        uint32_t toggle = 0;
        bool doToggle = false;
        while (clipper.Step())
            for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
                const auto& [name, id] = cl.manifest[visible[r]];
                ImGui::PushID(r);
                swatch(id);
                const bool sel = std::binary_search(m_cryptoSel.begin(), m_cryptoSel.end(), id);
                if (ImGui::Selectable(name.c_str(), sel)) { toggle = id; doToggle = true; }
                ImGui::PopID();
            }
        ImGui::EndChild();
        if (doToggle) toggleCryptoId(toggle);
    }
    ImGui::End();
}

void App::loadMatteDialog()
{
    const std::wstring path = ShowOpenFilteredDialog(m_hwnd, FromUtf8(tr(S::LoadMatte)).c_str(), L"OpenEXR", L"*.exr");
    if (!path.empty()) loadMatteSequence(path);
}

void App::loadMatteSequence(const std::wstring& path)
{
    if (!m_seq) return;
    int start = 0;
    Sequence seq = DetectSequence(path, &start);
    ExrInfo info;
    if (!seq.empty() && GetFileExtension(seq.frames[0].path) == L".exr") info = ReadExrInfo(seq.frames[start].path);
    if (info.cryptos.empty()) {
        showToast(std::string(tr(S::NoCryptoInFile)) + " " + ToUtf8(GetFileName(path)), true);
        return;
    }
    m_matteSeq = std::make_shared<Sequence>(std::move(seq));
    m_matteInfo = std::move(info);

    // Match frames by number; sequences with unrelated numbering are matched by position.
    auto files = std::make_shared<std::vector<std::wstring>>(m_seq->count());
    int hits = 0;
    for (int i = 0; i < m_seq->count(); ++i) {
        const int number = m_seq->frames[i].number;
        auto it = std::lower_bound(m_matteSeq->frames.begin(), m_matteSeq->frames.end(), number,
                                   [](const SequenceFrame& f, int n) { return f.number < n; });
        if (it != m_matteSeq->frames.end() && it->number == number) { (*files)[i] = it->path; ++hits; }
    }
    if (hits == 0)
        for (int i = 0; i < m_seq->count() && i < m_matteSeq->count(); ++i) (*files)[i] = m_matteSeq->frames[i].path;
    m_matteMissing = (int)std::count(files->begin(), files->end(), std::wstring());
    m_matteFiles = files;

    m_crypto = 0;
    m_cryptoSel.clear();
    m_lastPick.clear();
    if (m_matte == MatteMode::Off) m_matte = MatteMode::Ids;
    m_cryptoPanel = true;
    applyLoadOptions();
    showToast(std::string(tr(S::MatteLoaded)) + ": " + ToUtf8(m_matteSeq->displayName()), m_matteMissing > 0);
}

void App::clearMatteSequence()
{
    if (!m_matteSeq) return;
    m_matteSeq.reset();
    m_matteInfo = ExrInfo();
    m_matteFiles.reset();
    m_matteMissing = 0;
    m_crypto = 0;
    m_cryptoSel.clear();
    m_lastPick.clear();
    if (!hasCrypto()) m_matte = MatteMode::Off;
    applyLoadOptions();
}

void App::applyStartupLayers(const StartupOptions& opts)
{
    if (!opts.matteSeq.empty()) loadMatteSequence(opts.matteSeq);
    for (size_t i = 0; i < m_exrInfo.layers.size(); ++i)
        if (!opts.layer.empty() && m_exrInfo.layers[i].label == opts.layer) m_layer = (int)i;
    for (size_t i = 0; i < cryptoLayers().size(); ++i)
        if (!opts.cryptoLayer.empty() && cryptoLayers()[i].name == opts.cryptoLayer) m_crypto = (int)i;
    if (hasCrypto() && !opts.cryptoSelect.empty()) {
        const CryptoLayer& cl = cryptoLayers()[m_crypto];
        size_t start = 0;
        while (start <= opts.cryptoSelect.size()) {
            const size_t comma = opts.cryptoSelect.find(',', start);
            const std::string name = opts.cryptoSelect.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
            for (const auto& [n, id] : cl.manifest)
                if (n == name && !std::binary_search(m_cryptoSel.begin(), m_cryptoSel.end(), id))
                    m_cryptoSel.insert(std::lower_bound(m_cryptoSel.begin(), m_cryptoSel.end(), id), id);
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
    }
    static const struct { const char* name; MatteMode mode; } modes[] = {
        { "ids", MatteMode::Ids }, { "overlay", MatteMode::Overlay }, { "masked", MatteMode::Masked }, { "matte", MatteMode::Matte },
    };
    for (auto& m : modes)
        if (opts.matte == m.name && hasCrypto()) {
            m_matte = m.mode;
            m_cryptoPanel = true;
        }
    applyLoadOptions();
}
