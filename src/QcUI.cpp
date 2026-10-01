// Quality check: pixel checks (NaN/Inf/negative, false color, zebra), viewer guides,
// scopes, and the viewer overlays of the A/B compare. Nothing here runs until it is used.
#include <GL/glew.h>
#include "App.h"
#include "I18n.h"
#include "Platform.h"

#include <windows.h>
#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>

namespace {

double Seconds()
{
    static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return double(t.QuadPart) / double(freq.QuadPart);
}

float Badge(ImDrawList* dl, ImVec2 pos, const char* text, ImU32 fg, float s)
{
    const ImVec2 ts = ImGui::CalcTextSize(text);
    dl->AddRectFilled(pos, ImVec2(pos.x + ts.x + 12 * s, pos.y + ts.y + 6 * s), IM_COL32(0, 0, 0, 150), 5 * s);
    dl->AddText(ImVec2(pos.x + 6 * s, pos.y + 3 * s), fg, text);
    return ts.x + 12 * s;
}

// Rec.709 luma and chroma of display values (0..255).
inline void YCbCr(int r, int g, int b, float& y, float& cb, float& cr)
{
    y = (0.2126f * r + 0.7152f * g + 0.0722f * b) / 255.0f;
    cb = (-0.1146f * r - 0.3854f * g + 0.5f * b) / 255.0f;
    cr = (0.5f * r - 0.4542f * g - 0.0458f * b) / 255.0f;
}

// Vectorscope position (0..1, y down) of a color.
inline ImVec2 VectorPos(float cb, float cr)
{
    return ImVec2(0.5f + cb, 0.5f - cr);
}

void UploadRGBA(unsigned& tex, const std::vector<uint8_t>& px, int w, int h)
{
    if (!tex) {
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}

}  // namespace

App::ScreenRect App::imageRect(float vx, float vy, float vw, float vh, int half) const
{
    ScreenRect r;
    if (!m_shown || !m_shown->valid()) return r;
    float ax = vx, aw = vw;
    if (half >= 0) {
        const float hw = std::floor(vw / 2);
        ax = half ? vx + hw : vx;
        aw = half ? vw - hw : hw;
    }
    const float w = m_shown->fullWidth() * m_zoom, h = m_shown->fullHeight() * m_zoom;
    // GLViewer::draw rounds in bottom-up framebuffer pixels.
    const float left = std::floor(aw * 0.5f + m_panX - w * 0.5f);
    const float bottom = std::floor(vh * 0.5f + m_panY - h * 0.5f);
    r.x0 = ax + left;
    r.x1 = r.x0 + w;
    r.y1 = vy + vh - bottom;
    r.y0 = r.y1 - h;
    return r;
}

// ---------------------------------------------------------------------------
// Pixel checks

void App::setCheck(CheckMode m)
{
    m_check = m_check == m ? CheckMode::Off : m;
    m_badImage.reset();
}

// NaN/Inf and negative pixels of the shown half-float image, counted when it is still.
void App::countBadPixels()
{
    if (m_check != CheckMode::BadPixels || stackActive() || !m_shown || !m_shown->valid() || m_playDir != 0 || m_badImage == m_shown) return;
    m_badImage = m_shown;
    m_badNan = m_badNeg = 0;
    if (m_shown->type != PixelType::F16) return;   // integer files hold neither
    const uint16_t* p = reinterpret_cast<const uint16_t*>(m_shown->data.data());
    const size_t n = size_t(m_shown->width) * m_shown->height;
    auto nanInf = [](uint16_t h) { return (h & 0x7C00) == 0x7C00; };
    auto negative = [](uint16_t h) { return (h & 0x8000) != 0 && (h & 0x7FFF) != 0; };
    size_t nan = 0, neg = 0;
    for (size_t i = 0; i < n; ++i, p += 4) {
        if (nanInf(p[0]) || nanInf(p[1]) || nanInf(p[2]) || nanInf(p[3])) ++nan;
        else if (negative(p[0]) || negative(p[1]) || negative(p[2])) ++neg;
    }
    m_badNan = nan;
    m_badNeg = neg;
}

// ---------------------------------------------------------------------------
// Menu

void App::drawQcMenu()
{
    const float s = m_dpiScale;
    const Settings& g = m_settings;
    const bool on = m_check != CheckMode::Off || m_scopesOpen || g.guideAspect || g.guideSafe || g.guideCenter || g.guideThirds;
    if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.29f, 0.56f, 1.0f, 0.45f));
    if (ImGui::Button("QC")) ImGui::OpenPopup("##qc");
    if (on) ImGui::PopStyleColor();
    ImGui::SetItemTooltip("%s", tr(S::QcTitle));
    if (!ImGui::BeginPopup("##qc")) return;
    ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);

    ImGui::SeparatorText(tr(S::Pixels));
    static const struct { CheckMode mode; S label; const char* key; } checks[] = {
        { CheckMode::Off, S::CheckOff, nullptr }, { CheckMode::BadPixels, S::CheckBad, "N" },
        { CheckMode::FalseColor, S::CheckFalse, "E" }, { CheckMode::Zebra, S::CheckZebra, "Z" },
    };
    for (const auto& c : checks)
        if (ImGui::MenuItem(tr(c.label), c.key, m_check == c.mode)) {
            m_check = c.mode;
            m_badImage.reset();
        }
    ImGui::TextDisabled("%s", tr(S::CheckHint));

    ImGui::SeparatorText(tr(S::Scopes));
    if (ImGui::MenuItem(tr(S::Scopes), "H", m_scopesOpen)) m_scopesOpen = !m_scopesOpen;

    ImGui::SeparatorText(tr(S::Guides));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr(S::GuideAspect));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110 * s);
    if (ImGui::BeginCombo("##guideaspect", g.guideAspect ? AspectLabel(g.guideAspect) : tr(S::GuideNone))) {
        if (ImGui::Selectable(tr(S::GuideNone), g.guideAspect == 0)) m_settings.guideAspect = 0;
        for (int i = 1; i < AspectCount(); ++i)
            if (ImGui::Selectable(AspectLabel(i), g.guideAspect == i)) m_settings.guideAspect = i;
        ImGui::EndCombo();
    }
    ImGui::MenuItem(tr(S::GuideSafe), nullptr, &m_settings.guideSafe);
    ImGui::MenuItem(tr(S::GuideThirds), nullptr, &m_settings.guideThirds);
    ImGui::MenuItem(tr(S::GuideCenter), nullptr, &m_settings.guideCenter);
    ImGui::PopItemFlag();

    ImGui::Separator();
    if (ImGui::MenuItem(tr(S::FrameReport))) m_reportOpen = true;
    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// Viewer overlays

void App::drawGuides(float vx, float vy, float vw, float vh)
{
    const Settings& g = m_settings;
    if (!m_seq || !m_shown || !m_shown->valid() || !(g.guideAspect || g.guideSafe || g.guideCenter || g.guideThirds)) return;
    const float s = m_dpiScale;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->PushClipRect(ImVec2(vx, vy), ImVec2(vx + vw, vy + vh), true);
    const bool sbs = sideBySide();
    for (int half = sbs ? 0 : -1; half <= (sbs ? 1 : -1); ++half) {
        const ScreenRect r = imageRect(vx, vy, vw, vh, half);
        int ax, ay, aw, ah;
        AspectRect(m_shown->fullWidth(), m_shown->fullHeight(), g.guideAspect, ax, ay, aw, ah);
        const ImVec2 a0(r.x0 + ax * m_zoom, r.y0 + ay * m_zoom), a1(a0.x + aw * m_zoom, a0.y + ah * m_zoom);
        if (g.guideAspect) {
            const ImU32 mask = IM_COL32(0, 0, 0, 190);
            dl->AddRectFilled(ImVec2(r.x0, r.y0), ImVec2(r.x1, a0.y), mask);
            dl->AddRectFilled(ImVec2(r.x0, a1.y), ImVec2(r.x1, r.y1), mask);
            dl->AddRectFilled(ImVec2(r.x0, a0.y), ImVec2(a0.x, a1.y), mask);
            dl->AddRectFilled(ImVec2(a1.x, a0.y), ImVec2(r.x1, a1.y), mask);
            dl->AddRect(a0, a1, IM_COL32(255, 255, 255, 90));
            dl->AddText(ImVec2(a0.x + 6 * s, a0.y + 4 * s), IM_COL32(255, 255, 255, 140), AspectLabel(g.guideAspect));
        }
        if (g.guideSafe)
            for (float pct : { 0.93f, 0.90f }) {
                const float ix = (a1.x - a0.x) * (1 - pct) * 0.5f, iy = (a1.y - a0.y) * (1 - pct) * 0.5f;
                dl->AddRect(ImVec2(a0.x + ix, a0.y + iy), ImVec2(a1.x - ix, a1.y - iy), pct > 0.92f ? IM_COL32(255, 255, 255, 110) : IM_COL32(255, 210, 90, 110));
            }
        if (g.guideThirds)
            for (int k = 1; k <= 2; ++k) {
                const float x = a0.x + (a1.x - a0.x) * k / 3.0f, y = a0.y + (a1.y - a0.y) * k / 3.0f;
                dl->AddLine(ImVec2(x, a0.y), ImVec2(x, a1.y), IM_COL32(255, 255, 255, 60));
                dl->AddLine(ImVec2(a0.x, y), ImVec2(a1.x, y), IM_COL32(255, 255, 255, 60));
            }
        if (g.guideCenter) {
            const ImVec2 c((a0.x + a1.x) * 0.5f, (a0.y + a1.y) * 0.5f);
            const float l = 12 * s;
            dl->AddLine(ImVec2(c.x - l, c.y), ImVec2(c.x + l, c.y), IM_COL32(255, 255, 255, 150), 1.5f * s);
            dl->AddLine(ImVec2(c.x, c.y - l), ImVec2(c.x, c.y + l), IM_COL32(255, 255, 255, 150), 1.5f * s);
        }
    }
    dl->PopClipRect();
}

void App::drawQcOverlays(float vx, float vy, float vw, float vh)
{
    const float s = m_dpiScale, pad = 10 * s;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    if (!m_seq || !m_shown || !m_shown->valid()) return;

    // False color legend (bottom right)
    if (m_check == CheckMode::FalseColor) {
        static const struct { ImU32 color; const char* label; } bands[] = {
            { IM_COL32(115, 0, 153, 255), "<2" }, { IM_COL32(26, 77, 255, 255), "2-10" }, { IM_COL32(64, 64, 64, 255), "10-40" },
            { IM_COL32(51, 204, 64, 255), "40-50" }, { IM_COL32(170, 170, 170, 255), "50-85" }, { IM_COL32(255, 217, 26, 255), "85-97" },
            { IM_COL32(255, 26, 26, 255), ">97%" },
        };
        const float bw = 40 * s, bh = 10 * s, th = ImGui::GetFontSize();
        const float x0 = vx + vw - pad - bw * 7 - 8 * s, y0 = vy + vh - pad - bh - th - 12 * s;
        dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + bw * 7 + 8 * s, y0 + bh + th + 12 * s), IM_COL32(0, 0, 0, 150), 5 * s);
        for (int i = 0; i < 7; ++i) {
            const float x = x0 + 4 * s + i * bw;
            dl->AddRectFilled(ImVec2(x, y0 + 4 * s), ImVec2(x + bw - 2 * s, y0 + 4 * s + bh), bands[i].color);
            const ImVec2 ts = ImGui::CalcTextSize(bands[i].label);
            dl->AddText(ImVec2(x + (bw - ts.x) * 0.5f, y0 + 6 * s + bh), IM_COL32(220, 220, 225, 255), bands[i].label);
        }
    }

    // A/B compare: labels, wipe line
    if (!compareActive()) return;
    const ImagePtr b = compareImage(m_shownSet);
    const std::string nameA = "A  " + ToUtf8(SequenceBaseName(*m_seq)), nameB = "B  " + ToUtf8(SequenceBaseName(*m_cmpSeq));
    const std::string missingB = "B  " + std::string(tr(S::LoadError));
    const bool bad = b && !b->valid();
    const float y = vy + pad + ImGui::GetFontSize() + 16 * s;
    auto label = [&](bool isB, float x, bool alignRight) {
        const std::string& t = isB ? (bad ? missingB : nameB) : nameA;
        const float w = ImGui::CalcTextSize(t.c_str()).x + 12 * s;
        Badge(dl, ImVec2(alignRight ? x - w : x, y), t.c_str(), isB && bad ? IM_COL32(255, 120, 120, 255) : IM_COL32(230, 230, 235, 255), s);
    };
    switch (m_cmpMode) {
    case CompareMode::Toggle:
        label(m_cmpSwap, vx + pad, false);
        break;
    case CompareMode::SideBySide: {
        const float hw = std::floor(vw / 2);
        label(m_cmpSwap, vx + pad, false);
        label(!m_cmpSwap, vx + hw + pad, false);
        dl->AddLine(ImVec2(vx + hw, vy), ImVec2(vx + hw, vy + vh), IM_COL32(0, 0, 0, 200), 2 * s);
        break;
    }
    case CompareMode::Difference: {
        char buf[256];
        snprintf(buf, sizeof(buf), "|A \xE2\x88\x92 B|  \xC3\x97%.0f", m_diffGain);
        Badge(dl, ImVec2(vx + pad, y), bad ? missingB.c_str() : buf, bad ? IM_COL32(255, 120, 120, 255) : IM_COL32(230, 230, 235, 255), s);
        break;
    }
    default: {   // wipe
        const ScreenRect r = imageRect(vx, vy, vw, vh);
        const float split = std::clamp(std::floor(r.x0 + (r.x1 - r.x0) * m_wipe), vx, vx + vw);
        const float top = std::max(vy, r.y0), bottom = std::min(vy + vh, r.y1);
        dl->AddLine(ImVec2(split, top), ImVec2(split, bottom), IM_COL32(255, 255, 255, 220), 2 * s);
        dl->AddCircleFilled(ImVec2(split, (top + bottom) * 0.5f), 7 * s, IM_COL32(255, 255, 255, 230));
        dl->AddCircle(ImVec2(split, (top + bottom) * 0.5f), 7 * s, IM_COL32(0, 0, 0, 160), 0, 1.5f * s);
        label(m_cmpSwap, split - 8 * s, true);
        label(!m_cmpSwap, split + 8 * s, false);
        break;
    }
    }
}

// ---------------------------------------------------------------------------
// Scopes: a small render of the viewer source, binned on the CPU.

void App::updateScopes()
{
    if (!m_scopesOpen || !m_seq || !m_shown || !m_shown->valid()) return;
    const double now = Seconds();
    if (m_playDir != 0 && now - m_scopesAt < 1.0 / 12.0) return;   // enough while playing
    m_scopesAt = now;
    std::vector<uint8_t> px;
    int w = 0, h = 0;
    if (!m_viewer.renderPreview(512, px, w, h)) {
        m_scopesValid = false;
        return;
    }
    const int N = 256;
    m_hist.assign(4 * N, 0.0f);
    std::vector<uint32_t> wave(size_t(N) * N * 3, 0), vec(size_t(N) * N, 0);
    std::vector<float> vecColor(size_t(N) * N * 3, 0.0f);
    for (int y = 0; y < h; ++y) {
        const uint8_t* row = px.data() + size_t(y) * w * 4;
        for (int x = 0; x < w; ++x) {
            const int r = row[x * 4], g = row[x * 4 + 1], b = row[x * 4 + 2];
            float Y, cb, cr;
            YCbCr(r, g, b, Y, cb, cr);
            m_hist[r] += 1;
            m_hist[N + g] += 1;
            m_hist[2 * N + b] += 1;
            m_hist[3 * N + std::min(255, (int)std::lround(Y * 255.0f))] += 1;
            const int col = x * N / w;
            wave[(size_t(255 - r) * N + col) * 3 + 0]++;
            wave[(size_t(255 - g) * N + col) * 3 + 1]++;
            wave[(size_t(255 - b) * N + col) * 3 + 2]++;
            const ImVec2 v = VectorPos(cb, cr);
            const int vx = std::clamp((int)(v.x * N), 0, N - 1), vy = std::clamp((int)(v.y * N), 0, N - 1);
            const size_t k = size_t(vy) * N + vx;
            vec[k]++;
            vecColor[k * 3] += r;
            vecColor[k * 3 + 1] += g;
            vecColor[k * 3 + 2] += b;
        }
    }
    // Density -> brightness, saturating where the image concentrates.
    const float kWave = 40.0f / std::max(1.0f, float(w) * h / N), kVec = 600.0f / std::max(1.0f, float(w) * h);
    m_wave.assign(size_t(N) * N * 4, 0);
    m_vector.assign(size_t(N) * N * 4, 0);
    for (size_t i = 0; i < size_t(N) * N; ++i) {
        for (int c = 0; c < 3; ++c) m_wave[i * 4 + c] = (uint8_t)std::lround(255.0f * (1.0f - std::exp(-float(wave[i * 3 + c]) * kWave)));
        m_wave[i * 4 + 3] = 255;
        if (vec[i]) {
            const float v = 1.0f - std::exp(-float(vec[i]) * kVec);
            float rgb[3] = { vecColor[i * 3] / vec[i], vecColor[i * 3 + 1] / vec[i], vecColor[i * 3 + 2] / vec[i] };
            const float m = std::max({ rgb[0], rgb[1], rgb[2], 1.0f });
            for (int c = 0; c < 3; ++c) m_vector[i * 4 + c] = (uint8_t)std::lround(std::min(255.0f, (40.0f + 215.0f * rgb[c] / m) * v));
        }
        m_vector[i * 4 + 3] = 255;
    }
    UploadRGBA(m_waveTex, m_wave, N, N);
    UploadRGBA(m_vectorTex, m_vector, N, N);
    m_scopesValid = true;
}

void App::drawScopes()
{
    if (!m_scopesOpen || !m_seq) return;
    const float s = m_dpiScale;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x - m_panelW - 390 * s, vp->Pos.y + m_topBarH + 50 * s), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(370 * s, 330 * s), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.94f);
    const std::string title = std::string(tr(S::Scopes)) + "###scopes";
    if (!ImGui::Begin(title.c_str(), &m_scopesOpen, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    int tab = -1;
    static int requested = [] { const wchar_t* t = _wgetenv(L"SP_TEST_SCOPE"); return t ? _wtoi(t) : -1; }();   // debug
    if (ImGui::BeginTabBar("##scopetabs")) {
        const S names[] = { S::Waveform, S::Histogram, S::Vectorscope };
        for (int k = 0; k < 3; ++k)
            if (ImGui::BeginTabItem(tr(names[k]), nullptr, requested == k ? ImGuiTabItemFlags_SetSelected : 0)) {
                tab = k;
                ImGui::EndTabItem();
            }
        requested = -1;
        ImGui::EndTabBar();
    }
    ImVec2 avail = ImGui::GetContentRegionAvail();
    avail.y -= ImGui::GetTextLineHeightWithSpacing();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 grid = IM_COL32(255, 255, 255, 40), gridText = IM_COL32(255, 255, 255, 90);
    if (m_scopesValid && avail.x > 20 && avail.y > 20) {
        if (tab == 0) {   // waveform: columns of the image, value up
            ImGui::Image(ImTextureRef((ImTextureID)(intptr_t)m_waveTex), avail);
            for (int k = 0; k <= 4; ++k) {
                const float y = p.y + avail.y * (1.0f - k / 4.0f);
                dl->AddLine(ImVec2(p.x, y), ImVec2(p.x + avail.x, y), grid);
                char buf[8];
                snprintf(buf, sizeof(buf), "%d", k * 25);
                dl->AddText(ImVec2(p.x + 3 * s, y - (k == 4 ? 0 : ImGui::GetFontSize())), gridText, buf);
            }
        } else if (tab == 1) {   // histogram: R, G, B and luma, ignoring the peaks at the extremes for the scale
            dl->AddRectFilled(p, ImVec2(p.x + avail.x, p.y + avail.y), IM_COL32(0, 0, 0, 255));
            float peak = 1.0f;
            for (int c = 0; c < 4; ++c)
                for (int i = 1; i < 255; ++i) peak = std::max(peak, m_hist[c * 256 + i]);
            static const ImU32 colors[] = { IM_COL32(255, 70, 70, 200), IM_COL32(70, 230, 90, 200), IM_COL32(80, 130, 255, 220),
                                            IM_COL32(235, 235, 235, 230) };
            for (int c = 0; c < 4; ++c) {
                ImVec2 pts[256];
                // Square-root scale: a large flat area (a white background) does not flatten the rest.
                for (int i = 0; i < 256; ++i)
                    pts[i] = ImVec2(p.x + avail.x * i / 255.0f,
                                    p.y + avail.y * (1.0f - std::min(1.0f, std::sqrt(m_hist[c * 256 + i] / peak) * 0.95f)));
                dl->AddPolyline(pts, 256, colors[c], 0, 1.3f * s);
            }
            for (int k = 1; k < 4; ++k) dl->AddLine(ImVec2(p.x + avail.x * k / 4, p.y), ImVec2(p.x + avail.x * k / 4, p.y + avail.y), grid);
            ImGui::Dummy(avail);
        } else if (tab == 2) {   // vectorscope: square, Cr up, Cb right
            const float side = std::min(avail.x, avail.y);
            p.x += (avail.x - side) * 0.5f;
            ImGui::SetCursorScreenPos(p);
            ImGui::Image(ImTextureRef((ImTextureID)(intptr_t)m_vectorTex), ImVec2(side, side));
            const ImVec2 c(p.x + side * 0.5f, p.y + side * 0.5f);
            dl->AddCircle(c, side * 0.5f, grid, 64);
            dl->AddCircle(c, side * 0.5f * 0.75f, grid, 64);
            dl->AddLine(ImVec2(p.x, c.y), ImVec2(p.x + side, c.y), grid);
            dl->AddLine(ImVec2(c.x, p.y), ImVec2(c.x, p.y + side), grid);
            // 75% color bar targets
            static const struct { int r, g, b; const char* name; } targets[] = {
                { 191, 0, 0, "R" }, { 191, 191, 0, "Yl" }, { 0, 191, 0, "G" }, { 0, 191, 191, "Cy" }, { 0, 0, 191, "B" }, { 191, 0, 191, "Mg" },
            };
            for (const auto& t : targets) {
                float Y, cb, cr;
                YCbCr(t.r, t.g, t.b, Y, cb, cr);
                const ImVec2 v = VectorPos(cb, cr);
                const ImVec2 q(p.x + v.x * side, p.y + v.y * side);
                dl->AddRect(ImVec2(q.x - 4 * s, q.y - 4 * s), ImVec2(q.x + 4 * s, q.y + 4 * s), IM_COL32(255, 255, 255, 110));
                dl->AddText(ImVec2(q.x + 6 * s, q.y - 6 * s), gridText, t.name);
            }
            // Skin tone line (about 123 degrees)
            const float a = 123.0f * 3.14159265f / 180.0f;
            dl->AddLine(c, ImVec2(c.x + std::cos(a) * side * 0.5f, c.y - std::sin(a) * side * 0.5f), IM_COL32(255, 200, 150, 90), 1.0f * s);
        }
    } else {
        ImGui::Dummy(avail);
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x, ImGui::GetCursorScreenPos().y));
    ImGui::TextDisabled("%s", tr(S::ScopesHint));
    ImGui::End();
}
