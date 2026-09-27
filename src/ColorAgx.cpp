// Built-in AgX view and Blender config discovery.
//
// AgX follows the formulation used by Blender / Filament / three.js
// (Rec.2020 working space, inset matrix, log2 encoding over
// [-12.47393, +4.026069] EV, polynomial sigmoid, outset matrix, 2.2 linearize)
// and is expressed with native OCIO transforms, so it runs in the same GPU
// shader as everything else and works with any config.

#include "ColorManager.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>

namespace {

const char* const kAgxViewNames[] = {
    "AgX \xC2\xB7 built-in",
    "AgX Punchy \xC2\xB7 built-in",
    "AgX Golden \xC2\xB7 built-in",
};

// Row-major 3x3 matrices (out = M * in).
const double kRec709ToRec2020[9] = {
    0.6274040, 0.3292820, 0.0433136,
    0.0690970, 0.9195400, 0.0113612,
    0.0163916, 0.0880132, 0.8955950,
};
const double kRec2020ToRec709[9] = {
     1.6604910, -0.5876411, -0.0728499,
    -0.1245505,  1.1328999, -0.0083494,
    -0.0181508, -0.1005789,  1.1187297,
};
const double kRec2020ToP3D65[9] = {
     1.3435783, -0.2821797, -0.0613986,
    -0.0652975,  1.0757879, -0.0104905,
     0.0028218, -0.0195985,  1.0167767,
};
const double kAgxInset[9] = {
    0.856627153315983, 0.0951212405381588, 0.0482516061458583,
    0.137318972929847, 0.761241990602591,  0.101439036467562,
    0.11189821299995,  0.0767994186031903, 0.811302368396859,
};
const double kAgxOutset[9] = {
     1.1271005818144368,  -0.11060664309660323, -0.016493938717834573,
    -0.1413297634984383,   1.157823702216272,   -0.016493938717834257,
    -0.14132976349843826, -0.11060664309660294,  1.2519364065950405,
};
const double kAgxMinEv = -12.47393;
const double kAgxMaxEv = 4.026069;

OCIO::MatrixTransformRcPtr Matrix3(const double m[9])
{
    const double m44[16] = {
        m[0], m[1], m[2], 0,
        m[3], m[4], m[5], 0,
        m[6], m[7], m[8], 0,
        0,    0,    0,    1,
    };
    auto t = OCIO::MatrixTransform::Create();
    t->setMatrix(m44);
    return t;
}

OCIO::RangeTransformRcPtr Clamp(double lo, double hi, bool withMax)
{
    auto r = OCIO::RangeTransform::Create();
    r->setStyle(OCIO::RANGE_CLAMP);
    r->setMinInValue(lo);
    r->setMinOutValue(lo);
    if (withMax) {
        r->setMaxInValue(hi);
        r->setMaxOutValue(hi);
    }
    return r;
}

// Default AgX contrast curve (6th order polynomial fit), applied on [0,1].
double AgxSigmoid(double x)
{
    const double x2 = x * x, x4 = x2 * x2;
    return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}

std::string Lower(std::string s)
{
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

}  // namespace

const std::vector<std::string>& ColorManager::AgxViewNames()
{
    static const std::vector<std::string> names(std::begin(kAgxViewNames), std::end(kAgxViewNames));
    return names;
}

AgxLook ColorManager::agxLook() const
{
    const auto& names = AgxViewNames();
    for (size_t i = 0; i < names.size(); ++i)
        if (view == names[i]) return AgxLook(i + 1);
    return AgxLook::None;
}

bool ColorManager::isBuiltinView(const std::string& v) const
{
    const auto& names = AgxViewNames();
    return std::find(names.begin(), names.end(), v) != names.end();
}

OCIO::GroupTransformRcPtr ColorManager::buildAgxTransform(AgxLook look, const std::string& src)
{
    auto group = OCIO::GroupTransform::Create();

    // 1. Source -> scene-linear Rec.2020.
    OCIO::ConstProcessorRcPtr toRec2020;
    for (const char* n : { "Linear Rec.2020", "lin_rec2020", "Utility - Linear - Rec.2020", "lin_rec2020_scene" })
        if (hasColorSpace(n)) {
            toRec2020 = m_config->getProcessor(src.c_str(), n);
            break;
        }
    if (!toRec2020) {
        // Through the interchange roles (aces_interchange / cie_xyz_d65_interchange).
        try {
            static const OCIO::ConstConfigRcPtr studio = OCIO::Config::CreateFromFile(DefaultBuiltinUri().c_str());
            toRec2020 = OCIO::Config::GetProcessorFromConfigs(m_config, src.c_str(), studio, "Linear Rec.2020");
        } catch (const OCIO::Exception&) {
        }
    }
    if (toRec2020) {
        group->appendTransform(toRec2020->createGroupTransform());
    } else {
        // Last resort: assume the config's scene_linear is linear Rec.709.
        auto cst = OCIO::ColorSpaceTransform::Create();
        cst->setSrc(src.c_str());
        cst->setDst(OCIO::ROLE_SCENE_LINEAR);
        group->appendTransform(cst);
        group->appendTransform(Matrix3(kRec709ToRec2020));
    }

    // 2. Exposure (dynamic, scene-linear).
    auto ec = OCIO::ExposureContrastTransform::Create();
    ec->setStyle(OCIO::EXPOSURE_CONTRAST_LINEAR);
    ec->setPivot(0.18);
    ec->makeExposureDynamic();
    group->appendTransform(ec);

    // 3. AgX core.
    group->appendTransform(Clamp(1e-10, 0, false));
    group->appendTransform(Matrix3(kAgxInset));
    auto log = OCIO::LogAffineTransform::Create();
    log->setBase(2.0);
    const double range = kAgxMaxEv - kAgxMinEv;
    const double logSlope[3] = { 1.0 / range, 1.0 / range, 1.0 / range };
    const double logOffset[3] = { -kAgxMinEv / range, -kAgxMinEv / range, -kAgxMinEv / range };
    log->setLogSideSlopeValue(logSlope);
    log->setLogSideOffsetValue(logOffset);
    group->appendTransform(log);
    group->appendTransform(Clamp(0, 1, true));

    const unsigned long lutSize = 4096;
    auto lut = OCIO::Lut1DTransform::Create(lutSize, false);
    for (unsigned long i = 0; i < lutSize; ++i) {
        const float y = (float)AgxSigmoid(double(i) / (lutSize - 1));
        lut->setValue(i, y, y, y);
    }
    group->appendTransform(lut);

    if (look == AgxLook::Punchy || look == AgxLook::Golden) {
        auto cdl = OCIO::CDLTransform::Create();
        const double slopeP[3] = { 1.0, 1.0, 1.0 }, powerP[3] = { 1.35, 1.35, 1.35 };
        const double slopeG[3] = { 1.0, 0.9, 0.5 }, powerG[3] = { 0.8, 0.8, 0.8 };
        const double offset[3] = { 0, 0, 0 };
        cdl->setSlope(look == AgxLook::Punchy ? slopeP : slopeG);
        cdl->setOffset(offset);
        cdl->setPower(look == AgxLook::Punchy ? powerP : powerG);
        cdl->setSat(look == AgxLook::Punchy ? 1.4 : 1.3);
        cdl->setStyle(OCIO::CDL_NO_CLAMP);
        group->appendTransform(cdl);
    }

    group->appendTransform(Matrix3(kAgxOutset));
    auto linearize = OCIO::ExponentTransform::Create();
    const double e22[4] = { 2.2, 2.2, 2.2, 1.0 };
    linearize->setValue(e22);
    group->appendTransform(linearize);

    // 4. Display: primaries + encoding picked from the selected display's name.
    const std::string d = Lower(display);
    const bool p3 = d.find("p3") != std::string::npos;
    const bool bt1886 = d.find("1886") != std::string::npos || d.find("rec.709") != std::string::npos ||
                        d.find("rec709") != std::string::npos || d.find("bt.709") != std::string::npos;
    group->appendTransform(Matrix3(p3 ? kRec2020ToP3D65 : kRec2020ToRec709));
    group->appendTransform(Clamp(0, 1, true));
    if (bt1886 && !p3) {
        auto enc = OCIO::ExponentTransform::Create();
        const double e24[4] = { 2.4, 2.4, 2.4, 1.0 };
        enc->setValue(e24);
        enc->setDirection(OCIO::TRANSFORM_DIR_INVERSE);
        group->appendTransform(enc);
    } else {
        auto enc = OCIO::ExponentWithLinearTransform::Create();
        const double gamma[4] = { 2.4, 2.4, 2.4, 1.0 }, off[4] = { 0.055, 0.055, 0.055, 0.0 };
        enc->setGamma(gamma);
        enc->setOffset(off);
        enc->setDirection(OCIO::TRANSFORM_DIR_INVERSE);
        group->appendTransform(enc);
    }

    // 5. Display gamma (dynamic), like the regular pipeline.
    auto gc = OCIO::ExposureContrastTransform::Create();
    gc->setStyle(OCIO::EXPOSURE_CONTRAST_VIDEO);
    gc->setPivot(1.0);
    gc->makeGammaDynamic();
    group->appendTransform(gc);
    return group;
}

const std::vector<ExternalConfigInfo>& ColorManager::BlenderConfigs()
{
    static const std::vector<ExternalConfigInfo> list = [] {
        namespace fs = std::filesystem;
        std::vector<ExternalConfigInfo> v;
        const wchar_t* pf = _wgetenv(L"ProgramFiles");
        if (!pf) return v;
        std::error_code ec;
        const fs::path root = fs::path(pf) / L"Blender Foundation";
        for (const auto& app : fs::directory_iterator(root, ec)) {
            if (!app.is_directory(ec)) continue;
            for (const auto& ver : fs::directory_iterator(app.path(), ec)) {
                const fs::path cfg = ver.path() / L"datafiles" / L"colormanagement" / L"config.ocio";
                if (ver.is_directory(ec) && fs::exists(cfg, ec)) {
                    const auto u8path = cfg.u8string();
                    const auto u8name = app.path().filename().u8string();
                    v.push_back({ std::string(u8path.begin(), u8path.end()), std::string(u8name.begin(), u8name.end()) });
                }
            }
        }
        std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.label > b.label; });
        return v;
    }();
    return list;
}
