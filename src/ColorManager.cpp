#include "ColorManager.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>

static std::string Between(const std::string& s, const std::string& start, char stop)
{
    size_t a = s.find(start);
    if (a == std::string::npos) return {};
    a += start.size();
    size_t b = s.find(stop, a);
    return s.substr(a, b == std::string::npos ? std::string::npos : b - a);
}

// "studio-config-v4.0.0_aces-v2.0_ocio-v2.4" -> "ACES 2.0 · Studio v4.0.0"
static std::string FriendlyBuiltinLabel(const std::string& name)
{
    std::string kind = name.substr(0, name.find("-config"));
    if (kind == "cg") kind = "CG";
    else if (!kind.empty()) kind[0] = (char)toupper(kind[0]);
    const std::string cfgVer = Between(name, "config-v", '_');
    const std::string aces = Between(name, "aces-v", '_');
    const std::string ocio = Between(name, "ocio-v", '_');
    std::string label = aces.empty() ? name : "ACES " + aces + " \xC2\xB7 " + kind;
    if (!cfgVer.empty()) label += " v" + cfgVer;
    if (!ocio.empty()) label += "  (OCIO " + ocio + ")";
    return label;
}

const std::vector<BuiltinConfigInfo>& ColorManager::Builtins()
{
    static const std::vector<BuiltinConfigInfo> list = [] {
        std::vector<BuiltinConfigInfo> v;
        const auto& reg = OCIO::BuiltinConfigRegistry::Get();
        for (size_t i = 0; i < reg.getNumBuiltinConfigs(); ++i) {
            BuiltinConfigInfo b;
            b.uri = std::string("ocio://") + reg.getBuiltinConfigName(i);
            b.label = FriendlyBuiltinLabel(reg.getBuiltinConfigName(i));
            b.uiName = reg.getBuiltinConfigUIName(i);
            b.recommended = reg.isBuiltinConfigRecommended(i);
            v.push_back(b);
        }
        // Newest first.
        std::reverse(v.begin(), v.end());
        std::stable_sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.recommended > b.recommended; });
        return v;
    }();
    return list;
}

std::string ColorManager::DefaultBuiltinUri()
{
    const auto& list = Builtins();
    std::string best;
    for (auto& b : list) {
        if (b.uri.find("studio-config") == std::string::npos) continue;
        if (b.uri.find("aces-v2") != std::string::npos && (best.empty() || b.uri > best)) best = b.uri;
    }
    if (best.empty())
        for (auto& b : list)
            if (b.uri.find("studio-config") != std::string::npos && b.uri > best) best = b.uri;
    return best.empty() ? "ocio://default" : best;
}

bool ColorManager::HasEnvConfig()
{
    const char* v = std::getenv("OCIO");
    return v && *v;
}

bool ColorManager::load(const std::string& source)
{
    try {
        OCIO::ConstConfigRcPtr cfg;
        if (source == "$OCIO") {
            if (!HasEnvConfig()) throw OCIO::Exception("$OCIO is not set");
            cfg = OCIO::Config::CreateFromEnv();
        } else {
            // Handles files, .ocioz archives and ocio:// URIs (incl. aliases like studio-config-latest).
            cfg = OCIO::Config::CreateFromFile(source.c_str());
        }
        cfg->validate();
        m_config = cfg;
        m_source = source;
        m_error.clear();
        rebuildLists();

        if (!hasColorSpace(input)) input.clear();
        if (std::find(m_displays.begin(), m_displays.end(), display) == m_displays.end()) display = defaultDisplay();
        auto v = views(display);
        if (!isBuiltinView(view) && std::find(v.begin(), v.end(), view) == v.end()) view = defaultView(display);
        if (std::find(m_looks.begin(), m_looks.end(), look) == m_looks.end()) look.clear();
        return true;
    } catch (const std::exception& e) {
        m_error = e.what();
        return false;
    }
}

std::string ColorManager::sourceLabel() const
{
    if (m_source == "$OCIO") return "$OCIO";
    if (m_source.rfind("ocio://", 0) == 0) {
        for (auto& b : Builtins())
            if (b.uri == m_source) return b.label.substr(0, b.label.find(" v"));
        return m_source.substr(7);
    }
    for (auto& b : BlenderConfigs())
        if (b.path == m_source) return b.label;
    size_t slash = m_source.find_last_of("\\/");
    std::string file = slash == std::string::npos ? m_source : m_source.substr(slash + 1);
    if (m_config && m_config->getName() && *m_config->getName()) return std::string(m_config->getName()) + " (" + file + ")";
    return file;
}

bool ColorManager::isCustomFile() const
{
    return !m_source.empty() && m_source != "$OCIO" && m_source.rfind("ocio://", 0) != 0;
}

void ColorManager::rebuildLists()
{
    m_colorSpaces.clear();
    m_displays.clear();
    m_looks.clear();
    const int n = m_config->getNumColorSpaces(OCIO::SEARCH_REFERENCE_SPACE_ALL, OCIO::COLORSPACE_ACTIVE);
    for (int i = 0; i < n; ++i) {
        const char* name = m_config->getColorSpaceNameByIndex(OCIO::SEARCH_REFERENCE_SPACE_ALL, OCIO::COLORSPACE_ACTIVE, i);
        auto cs = m_config->getColorSpace(name);
        ColorSpaceInfo info;
        info.name = name;
        if (cs) {
            info.family = cs->getFamily() ? cs->getFamily() : "";
            info.description = cs->getDescription() ? cs->getDescription() : "";
        }
        m_colorSpaces.push_back(std::move(info));
    }
    for (int i = 0; i < m_config->getNumDisplays(); ++i) m_displays.push_back(m_config->getDisplay(i));
    for (int i = 0; i < m_config->getNumLooks(); ++i) m_looks.push_back(m_config->getLookNameByIndex(i));
}

std::vector<std::string> ColorManager::views(const std::string& d) const
{
    std::vector<std::string> v;
    if (!m_config || d.empty()) return v;
    for (int i = 0; i < m_config->getNumViews(d.c_str()); ++i) v.push_back(m_config->getView(d.c_str(), i));
    return v;
}

bool ColorManager::hasColorSpace(const std::string& name) const
{
    return findColorSpace(name) != nullptr;
}

const ColorSpaceInfo* ColorManager::findColorSpace(const std::string& name) const
{
    for (auto& c : m_colorSpaces)
        if (c.name == name) return &c;
    return nullptr;
}

std::string ColorManager::defaultInput(bool floatFormat, const std::string& utf8Path) const
{
    if (!m_config) return {};
    auto roleSpace = [&](const char* role) -> std::string {
        if (!m_config->hasRole(role)) return {};
        auto cs = m_config->getColorSpace(role);
        return cs ? cs->getName() : std::string();
    };

    // Custom configs often encode studio conventions in file rules: trust them
    // unless they only resolve to the catch-all default rule. Built-in configs'
    // rules are generic (ACES2065-1 for everything), so they are skipped.
    if (!utf8Path.empty() && m_source.rfind("ocio://", 0) != 0) {
        size_t ruleIndex = 0;
        const char* cs = m_config->getColorSpaceFromFilepath(utf8Path.c_str(), ruleIndex);
        auto rules = m_config->getFileRules();
        const bool isDefaultRule = rules && ruleIndex == rules->getNumEntries() - 1;
        if (cs && *cs && !isDefaultRule && hasColorSpace(cs)) return cs;
    }

    if (floatFormat) {
        std::string s = roleSpace("scene_linear");
        if (!s.empty()) return s;
    } else {
        static const char* candidates[] = {
            "sRGB Encoded Rec.709 (sRGB)", "sRGB - Texture", "Utility - sRGB - Texture",
            "Input - Generic - sRGB - Texture", "srgb_tx", "srgb_texture", "sRGB", "srgb",
        };
        for (auto c : candidates)
            if (hasColorSpace(c)) return c;
        for (auto role : { "color_picking", "texture_paint", "matte_paint" }) {
            std::string s = roleSpace(role);
            if (!s.empty()) return s;
        }
    }
    std::string s = roleSpace("default");
    if (!s.empty()) return s;
    return m_colorSpaces.empty() ? std::string() : m_colorSpaces.front().name;
}

std::string ColorManager::defaultDisplay() const
{
    if (!m_config) return {};
    const char* d = m_config->getDefaultDisplay();
    return d ? d : std::string();
}

std::string ColorManager::defaultView(const std::string& d) const
{
    if (!m_config || d.empty()) return {};
    const char* v = m_config->getDefaultView(d.c_str());
    return v ? v : std::string();
}

std::string ColorManager::workingSpace() const
{
    if (!m_config || !m_config->hasRole(OCIO::ROLE_SCENE_LINEAR)) return {};
    auto cs = m_config->getColorSpace(OCIO::ROLE_SCENE_LINEAR);
    return cs ? cs->getName() : std::string();
}

OCIO::ConstGPUProcessorRcPtr ColorManager::buildConversion(const std::string& src, const std::string& dst)
{
    if (!m_config) return nullptr;
    try {
        auto proc = m_config->getProcessor(src.c_str(), dst.c_str());
        m_error.clear();
        return proc->getOptimizedGPUProcessor(OCIO::OPTIMIZATION_DEFAULT);
    } catch (const std::exception& e) {
        m_error = e.what();
        return nullptr;
    }
}

OCIO::ConstGPUProcessorRcPtr ColorManager::buildGpuProcessor(const std::string& src)
{
    if (!m_config) return nullptr;
    if (agxLook() != AgxLook::None) {
        try {
            auto proc = m_config->getProcessor(buildAgxTransform(agxLook(), src));
            m_error.clear();
            return proc->getOptimizedGPUProcessor(OCIO::OPTIMIZATION_DEFAULT);
        } catch (const std::exception& e) {
            m_error = e.what();
            return nullptr;
        }
    }
    try {
        auto dvt = OCIO::DisplayViewTransform::Create();
        dvt->setSrc(src.c_str());
        dvt->setDisplay(display.c_str());
        dvt->setView(view.c_str());

        auto makePipeline = [&](bool withLinearCC) {
            auto vp = OCIO::LegacyViewingPipeline::Create();
            vp->setDisplayViewTransform(dvt);
            if (!look.empty()) {
                vp->setLooksOverrideEnabled(true);
                vp->setLooksOverride(look.c_str());
            }
            if (withLinearCC) {
                auto ec = OCIO::ExposureContrastTransform::Create();
                ec->setStyle(OCIO::EXPOSURE_CONTRAST_LINEAR);
                ec->setPivot(0.18);
                ec->makeExposureDynamic();
                vp->setLinearCC(ec);
            }
            auto gc = OCIO::ExposureContrastTransform::Create();
            gc->setStyle(OCIO::EXPOSURE_CONTRAST_VIDEO);
            gc->setPivot(1.0);
            gc->makeGammaDynamic();
            vp->setDisplayCC(gc);
            return vp->getProcessor(m_config, m_config->getCurrentContext());
        };

        OCIO::ConstProcessorRcPtr proc;
        try {
            proc = makePipeline(true);
        } catch (const OCIO::Exception&) {
            proc = makePipeline(false);   // config without scene_linear role
        }
        m_error.clear();
        return proc->getOptimizedGPUProcessor(OCIO::OPTIMIZATION_DEFAULT);
    } catch (const std::exception& e) {
        m_error = e.what();
        return nullptr;
    }
}
