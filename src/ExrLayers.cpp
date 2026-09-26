// Multi-layer / multi-part EXR support and Cryptomatte decoding.
#include "ExrLayers.h"
#include "Platform.h"

#include <ImfChannelList.h>
#include <ImfFrameBuffer.h>
#include <ImfHeader.h>
#include <ImfInputPart.h>
#include <ImfMultiPartInputFile.h>
#include <ImfPartType.h>
#include <ImfStandardAttributes.h>
#include <ImfStringAttribute.h>
#include <ImathBox.h>
#include <half.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace {

std::string Lower(std::string s)
{
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string Suffix(const std::string& channel)
{
    const size_t dot = channel.find_last_of('.');
    return dot == std::string::npos ? channel : channel.substr(dot + 1);
}

std::string Prefix(const std::string& channel)
{
    const size_t dot = channel.find_last_of('.');
    return dot == std::string::npos ? std::string() : channel.substr(0, dot);
}

bool IsColorSuffix(const std::string& s)
{
    const std::string l = Lower(s);
    return l == "r" || l == "g" || l == "b" || l == "a" || l == "red" || l == "green" || l == "blue" || l == "alpha";
}

// Maps a layer's channels onto R, G, B, A slots. Single-channel layers become gray.
void MapChannels(const std::vector<std::string>& channels, std::string out[4], bool& gray)
{
    for (int i = 0; i < 4; ++i) out[i].clear();
    gray = false;
    auto find = [&](std::initializer_list<const char*> names) -> std::string {
        for (const auto& c : channels) {
            const std::string s = Lower(Suffix(c));
            for (const char* n : names)
                if (s == n) return c;
        }
        return {};
    };
    out[3] = find({ "a", "alpha" });
    const char* sets[][3] = { { "r", "g", "b" }, { "red", "green", "blue" }, { "x", "y", "z" }, { "u", "v", "w" } };
    for (auto& set : sets) {
        std::string r = find({ set[0] }), g = find({ set[1] }), b = find({ set[2] });
        if (!r.empty() || !g.empty() || !b.empty()) {
            out[0] = r; out[1] = g; out[2] = b;
            if (!out[0].empty() && !out[1].empty()) return;
        }
    }
    std::vector<std::string> rest;
    for (const auto& c : channels)
        if (c != out[3]) rest.push_back(c);
    if (rest.size() == 1) {
        out[0] = rest[0];
        gray = true;
    } else {
        for (size_t i = 0; i < rest.size() && i < 3; ++i) out[i] = rest[i];
    }
}

// Minimal parser for the flat {"name": "hex", ...} Cryptomatte manifest.
bool ParseJsonString(const std::string& s, size_t& i, std::string& out)
{
    while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
    if (i >= s.size() || s[i] != '"') return false;
    ++i;
    out.clear();
    while (i < s.size() && s[i] != '"') {
        char c = s[i++];
        if (c == '\\' && i < s.size()) {
            const char e = s[i++];
            switch (e) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'u': {
                if (i + 4 > s.size()) return false;
                const unsigned cp = (unsigned)std::stoul(s.substr(i, 4), nullptr, 16);
                i += 4;
                if (cp < 0x80) out += (char)cp;
                else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
                else { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
                break;
            }
            default: out += e; break;   // \" \\ \/
            }
        } else {
            out += c;
        }
    }
    if (i >= s.size()) return false;
    ++i;
    return true;
}

std::vector<std::pair<std::string, uint32_t>> ParseManifest(const std::string& json)
{
    std::vector<std::pair<std::string, uint32_t>> out;
    size_t i = json.find('{');
    if (i == std::string::npos) return out;
    ++i;
    for (;;) {
        std::string key, value;
        if (!ParseJsonString(json, i, key)) break;
        while (i < json.size() && (std::isspace((unsigned char)json[i]) || json[i] == ':')) ++i;
        if (!ParseJsonString(json, i, value)) break;
        try {
            if (!key.empty()) out.push_back({ key, (uint32_t)std::stoul(value, nullptr, 16) });
        } catch (...) {
        }
        while (i < json.size() && (std::isspace((unsigned char)json[i]) || json[i] == ',')) ++i;
        if (i >= json.size() || json[i] == '}') break;
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool IsDeep(const Imf::Header& h)
{
    return h.hasType() && (h.type() == Imf::DEEPSCANLINE || h.type() == Imf::DEEPTILE);
}

}  // namespace

std::string CryptoLayer::nameOf(uint32_t id) const
{
    for (const auto& [n, v] : manifest)
        if (v == id) return n;
    char buf[16];
    snprintf(buf, sizeof(buf), "%08x", id);
    return buf;
}

void CryptoIdColor(uint32_t id, float rgb[3])
{
    uint32_t h = id;
    h ^= h >> 16; h *= 0x7feb352dU;
    h ^= h >> 15; h *= 0x846ca68bU;
    h ^= h >> 16;
    rgb[0] = 0.12f + 0.88f * float(h & 0xFF) / 255.0f;
    rgb[1] = 0.12f + 0.88f * float((h >> 8) & 0xFF) / 255.0f;
    rgb[2] = 0.12f + 0.88f * float((h >> 16) & 0xFF) / 255.0f;
}

// ---------------------------------------------------------------------------

ExrInfo ReadExrInfo(const std::wstring& path)
{
    ExrInfo info;
    try {
        Imf::MultiPartInputFile file(ToUtf8(path).c_str());
        const bool multiPart = file.parts() > 1;
        for (int p = 0; p < file.parts(); ++p) {
            const Imf::Header& hdr = file.header(p);
            if (IsDeep(hdr)) continue;
            const std::string partName = hdr.hasName() ? hdr.name() : std::string();

            std::vector<std::string> channels;
            for (auto it = hdr.channels().begin(); it != hdr.channels().end(); ++it) channels.push_back(it.name());

            // Cryptomatte layers from metadata: cryptomatte/<key>/name, .../manifest, .../manif_file
            std::map<std::string, std::string> cryptoNames, manifests, manifFiles;
            for (auto it = hdr.begin(); it != hdr.end(); ++it) {
                const std::string n = it.name();
                if (n.rfind("cryptomatte/", 0) != 0) continue;
                const auto* sa = dynamic_cast<const Imf::StringAttribute*>(&it.attribute());
                if (!sa) continue;
                const size_t slash = n.find('/', 12);
                if (slash == std::string::npos) continue;
                const std::string key = n.substr(12, slash - 12), field = n.substr(slash + 1);
                if (field == "name") cryptoNames[key] = sa->value();
                else if (field == "manifest") manifests[key] = sa->value();
                else if (field == "manif_file") manifFiles[key] = sa->value();
            }
            // Fallback: "<name>00.R" style channels without metadata.
            std::set<std::string> prefixes;
            for (auto& c : channels) prefixes.insert(Prefix(c));
            for (auto& pre : prefixes)
                if (pre.size() > 2 && pre.compare(pre.size() - 2, 2, "00") == 0) {
                    const std::string base = pre.substr(0, pre.size() - 2);
                    bool known = false;
                    for (auto& [k, v] : cryptoNames) known |= v == base;
                    if (!known && base.find("rypto") != std::string::npos) cryptoNames["_" + base] = base;
                }

            // Channel of `base` with the given component, matched case-insensitively.
            auto component = [&](const std::string& base, const char* a, const char* b) -> std::string {
                for (auto& c : channels)
                    if (Prefix(c) == base) {
                        const std::string s = Lower(Suffix(c));
                        if (s == a || s == b) return c;
                    }
                return {};
            };
            std::set<std::string> cryptoPrefixes;
            for (auto& [key, name] : cryptoNames) {
                CryptoLayer cl;
                cl.part = p;
                cl.name = name;
                for (int k = 0; k < 16; ++k) {
                    char base[256];
                    snprintf(base, sizeof(base), "%s%02d", name.c_str(), k);
                    if (!prefixes.count(base)) break;
                    cl.channels.push_back(component(base, "r", "red"));
                    cl.channels.push_back(component(base, "g", "green"));
                    cl.channels.push_back(component(base, "b", "blue"));
                    cl.channels.push_back(component(base, "a", "alpha"));
                    cryptoPrefixes.insert(base);
                }
                if (cl.channels.empty()) continue;
                cryptoPrefixes.insert(name);   // preview channels "CryptoObject.R/G/B"
                std::string json = manifests.count(key) ? manifests[key] : std::string();
                if (json.empty() && manifFiles.count(key)) {
                    std::ifstream f(GetParentDir(path) + L"\\" + FromUtf8(manifFiles[key]));
                    std::stringstream ss;
                    ss << f.rdbuf();
                    json = ss.str();
                }
                cl.manifest = ParseManifest(json);
                info.cryptos.push_back(std::move(cl));
            }

            // Regular layers, grouped by prefix. Root color channels form "RGBA";
            // other root channels (Z, depth...) are single-channel layers.
            std::map<std::string, std::vector<std::string>> groups;
            std::vector<std::string> order;
            std::vector<std::string> rootColor, rootOther;
            for (auto& c : channels) {
                const std::string pre = Prefix(c);
                if (cryptoPrefixes.count(pre)) continue;
                if (pre.empty()) {
                    (IsColorSuffix(c) ? rootColor : rootOther).push_back(c);
                    continue;
                }
                if (!groups.count(pre)) order.push_back(pre);
                groups[pre].push_back(c);
            }
            auto label = [&](const std::string& pre) {
                if (!multiPart || partName.empty()) return pre;
                if (pre.empty() || pre == partName) return partName;
                return partName + "." + pre;
            };
            if (!rootColor.empty()) {
                ExrLayer l;
                l.part = p;
                l.label = multiPart && !partName.empty() ? partName : "RGBA";
                l.channels = rootColor;
                l.isRootColor = p == 0;
                info.layers.push_back(l);
            }
            for (auto& c : rootOther) info.layers.push_back({ p, label(c), { c }, false });
            for (auto& pre : order) info.layers.push_back({ p, label(pre), groups[pre], false });
        }
    } catch (const std::exception& e) {
        Log("ReadExrInfo failed: %s", e.what());
    }
    info.defaultLayer = 0;
    for (size_t i = 0; i < info.layers.size(); ++i)
        if (info.layers[i].isRootColor) { info.defaultLayer = (int)i; break; }
    return info;
}

// ---------------------------------------------------------------------------

namespace {

// Reads up to 4 channels of one part as half into a data-window-sized RGBA
// buffer, then copies the part that overlaps the display window into the canvas.
void ReadLayerIntoCanvas(Imf::MultiPartInputFile& file, int part, const std::vector<std::string>& channels,
                         const Imath::Box2i& disp, half* canvas)
{
    Imf::InputPart in(file, part);
    const Imath::Box2i dw = in.header().dataWindow();
    const int w = dw.max.x - dw.min.x + 1, h = dw.max.y - dw.min.y + 1;
    const int cw = disp.max.x - disp.min.x + 1;

    std::string map[4];
    bool gray = false;
    MapChannels(channels, map, gray);

    const size_t px = sizeof(half) * 4;
    std::vector<half> tmp(size_t(w) * h * 4);
    char* base = reinterpret_cast<char*>(tmp.data()) - (ptrdiff_t(dw.min.x) + ptrdiff_t(dw.min.y) * w) * ptrdiff_t(px);
    Imf::FrameBuffer fb;
    const float fills[4] = { 0, 0, 0, 1 };
    for (int c = 0; c < 4; ++c) {
        // A slice whose channel is missing gets its fill value.
        const std::string name = map[c].empty() ? "__missing_" + std::to_string(c) : map[c];
        fb.insert(name, Imf::Slice(Imf::HALF, base + c * sizeof(half), px, px * w, 1, 1, fills[c]));
    }
    in.setFrameBuffer(fb);
    in.readPixels(dw.min.y, dw.max.y);

    const int x0 = std::max(dw.min.x, disp.min.x), x1 = std::min(dw.max.x, disp.max.x);
    const int y0 = std::max(dw.min.y, disp.min.y), y1 = std::min(dw.max.y, disp.max.y);
    for (int y = y0; y <= y1 && x0 <= x1; ++y) {
        const half* src = tmp.data() + (size_t(y - dw.min.y) * w + (x0 - dw.min.x)) * 4;
        half* dst = canvas + (size_t(y - disp.min.y) * cw + (x0 - disp.min.x)) * 4;
        if (!gray) {
            std::memcpy(dst, src, size_t(x1 - x0 + 1) * px);
        } else {
            for (int x = x0; x <= x1; ++x, src += 4, dst += 4) {
                dst[0] = dst[1] = dst[2] = src[0];
                dst[3] = src[3];
            }
        }
    }
}

// Inserts the Cryptomatte rank channels (id, coverage pairs) as FLOAT slices.
void InsertCryptoSlices(Imf::FrameBuffer& fb, const LoadOptions& opt, char* base, size_t stride, size_t yStride)
{
    for (size_t i = 0; i < opt.cryptoChannels.size(); ++i) {
        const std::string& name = opt.cryptoChannels[i];
        fb.insert(name.empty() ? "__missing_" + std::to_string(i) : name,
                  Imf::Slice(Imf::FLOAT, base + i * sizeof(float), stride, yStride, 1, 1, 0.0f));
    }
}

// Decoded Cryptomatte over a display window: selection coverage and, optionally, ID colors.
struct CryptoMask {
    int width = 0, height = 0;
    std::vector<float> mask;   // width * height
    std::vector<float> rgb;    // width * height * 3 (cryptoColors only)
};

// Decodes Cryptomatte ranks in row bands. Pixels outside the data window have no coverage.
CryptoMask DecodeCrypto(Imf::MultiPartInputFile& file, const LoadOptions& opt, const Imath::Box2i& disp)
{
    CryptoMask out;
    out.width = disp.max.x - disp.min.x + 1;
    out.height = disp.max.y - disp.min.y + 1;
    out.mask.assign(size_t(out.width) * out.height, 0.0f);
    if (opt.cryptoColors) out.rgb.assign(size_t(out.width) * out.height * 3, 0.0f);

    Imf::InputPart in(file, std::clamp(opt.cryptoPart, 0, file.parts() - 1));
    const Imath::Box2i dw = in.header().dataWindow();
    const int w = dw.max.x - dw.min.x + 1;
    const int ranks = (int)opt.cryptoChannels.size() / 2;
    const size_t stride = sizeof(float) * ranks * 2;   // (id, coverage) per rank
    const int band = 64;
    std::vector<float> buf(size_t(w) * band * ranks * 2);

    const std::vector<uint32_t>& sel = opt.selection;
    for (int by = dw.min.y; by <= dw.max.y; by += band) {
        const int ey = std::min(dw.max.y, by + band - 1);
        char* base = reinterpret_cast<char*>(buf.data()) - (ptrdiff_t(dw.min.x) + ptrdiff_t(by) * w) * ptrdiff_t(stride);
        Imf::FrameBuffer fb;
        InsertCryptoSlices(fb, opt, base, stride, stride * w);
        in.setFrameBuffer(fb);
        in.readPixels(by, ey);

        for (int y = std::max(by, disp.min.y); y <= std::min(ey, disp.max.y); ++y) {
            const float* row = buf.data() + size_t(y - by) * w * ranks * 2;
            for (int x = std::max(dw.min.x, disp.min.x); x <= std::min(dw.max.x, disp.max.x); ++x) {
                const float* p = row + size_t(x - dw.min.x) * ranks * 2;
                float mask = 0, col[3] = { 0, 0, 0 };
                for (int r = 0; r < ranks; ++r) {
                    const float cov = p[r * 2 + 1];
                    if (cov == 0.0f) continue;
                    uint32_t id;
                    std::memcpy(&id, &p[r * 2], 4);
                    if (!sel.empty() && std::binary_search(sel.begin(), sel.end(), id)) mask += cov;
                    if (opt.cryptoColors) {
                        float c[3];
                        CryptoIdColor(id, c);
                        col[0] += c[0] * cov; col[1] += c[1] * cov; col[2] += c[2] * cov;
                    }
                }
                const size_t o = size_t(y - disp.min.y) * out.width + (x - disp.min.x);
                out.mask[o] = std::clamp(mask, 0.0f, 1.0f);
                if (opt.cryptoColors) { out.rgb[o * 3] = col[0]; out.rgb[o * 3 + 1] = col[1]; out.rgb[o * 3 + 2] = col[2]; }
            }
        }
    }
    return out;
}

// Writes a decoded mask into the alpha (and ID colors into RGB) of a cw x ch half
// RGBA canvas, with nearest-neighbour scaling when the sizes differ.
void CompositeCrypto(const CryptoMask& m, half* canvas, int cw, int ch)
{
    for (int y = 0; y < ch; ++y) {
        const int sy = m.height == ch ? y : std::min(m.height - 1, int(int64_t(y) * m.height / ch));
        for (int x = 0; x < cw; ++x) {
            const int sx = m.width == cw ? x : std::min(m.width - 1, int(int64_t(x) * m.width / cw));
            const size_t o = size_t(sy) * m.width + sx;
            half* dst = canvas + (size_t(y) * cw + x) * 4;
            dst[3] = half(m.mask[o]);
            if (!m.rgb.empty()) { dst[0] = half(m.rgb[o * 3]); dst[1] = half(m.rgb[o * 3 + 1]); dst[2] = half(m.rgb[o * 3 + 2]); }
        }
    }
}

Imath::Box2i DisplayWindow(const Imf::Header& h)
{
    Imath::Box2i d = h.displayWindow();
    return d.isEmpty() ? h.dataWindow() : d;
}

}  // namespace

ImagePtr LoadExrWithOptions(const std::wstring& path, const LoadOptions& opt)
{
    auto img = std::make_shared<Image>();
    try {
        Imf::MultiPartInputFile file(ToUtf8(path).c_str());
        const int part = std::clamp(opt.part, 0, file.parts() - 1);
        const Imf::Header& hdr = file.header(part);
        Imath::Box2i disp = hdr.displayWindow();
        if (disp.isEmpty()) disp = hdr.dataWindow();
        img->width = disp.max.x - disp.min.x + 1;
        img->height = disp.max.y - disp.min.y + 1;
        img->type = PixelType::F16;
        img->data.assign(size_t(img->width) * img->height * 4 * sizeof(half), 0);
        half* canvas = reinterpret_cast<half*>(img->data.data());

        std::vector<std::string> channels = opt.channels;
        if (channels.empty())   // default layer with Cryptomatte on: root color channels
            for (auto it = hdr.channels().begin(); it != hdr.channels().end(); ++it)
                if (Prefix(it.name()).empty() && IsColorSuffix(it.name())) channels.push_back(it.name());
        if (!(opt.cryptoActive() && opt.cryptoColors)) ReadLayerIntoCanvas(file, part, channels, disp, canvas);
        if (opt.cryptoActive()) CompositeCrypto(DecodeCrypto(file, opt, disp), canvas, img->width, img->height);

        std::string map[4];
        bool gray = false;
        MapChannels(channels, map, gray);
        img->hasAlpha = !map[3].empty() || opt.cryptoActive();
        img->description = "EXR";
        if (!opt.channels.empty()) {
            const std::string pre = Prefix(opt.channels[0]);
            img->description += " \xC2\xB7 " + (pre.empty() ? opt.channels[0] : pre);
        }
        if (opt.cryptoActive()) img->description += " \xC2\xB7 Cryptomatte";
    } catch (const std::exception& e) {
        img = std::make_shared<Image>();
        img->error = e.what();
    }
    return img;
}

ImagePtr ApplyExternalCrypto(const ImagePtr& frame, const std::wstring& mattePath, const LoadOptions& opt)
{
    auto img = std::make_shared<Image>();
    img->width = frame->width;
    img->height = frame->height;
    img->type = PixelType::F16;
    img->hasAlpha = true;
    img->description = frame->description + " \xC2\xB7 Cryptomatte";
    const size_t n = size_t(img->width) * img->height * 4;
    img->data.resize(n * sizeof(half));
    half* dst = reinterpret_cast<half*>(img->data.data());
    switch (frame->type) {
    case PixelType::U8:
        for (size_t i = 0; i < n; ++i) dst[i] = half(frame->data[i] * (1.0f / 255.0f));
        break;
    case PixelType::U16: {
        const uint16_t* src = reinterpret_cast<const uint16_t*>(frame->data.data());
        for (size_t i = 0; i < n; ++i) dst[i] = half(src[i] * (1.0f / 65535.0f));
        break;
    }
    case PixelType::F16:
        std::memcpy(dst, frame->data.data(), n * sizeof(half));
        break;
    }

    CryptoMask m;
    if (!mattePath.empty()) {
        try {
            Imf::MultiPartInputFile file(ToUtf8(mattePath).c_str());
            m = DecodeCrypto(file, opt, DisplayWindow(file.header(std::clamp(opt.cryptoPart, 0, file.parts() - 1))));
        } catch (const std::exception& e) {
            Log("External Cryptomatte failed: %s", e.what());
            m = CryptoMask();
        }
    }
    if (m.mask.empty()) {   // no matte for this frame: nothing selected
        img->description += " (-)";
        m.width = m.height = 1;
        m.mask.assign(1, 0.0f);
        if (opt.cryptoColors) m.rgb.assign(3, 0.0f);
    }
    CompositeCrypto(m, dst, img->width, img->height);
    return img;
}

bool PickCryptoId(const std::wstring& cryptoPath, const LoadOptions& opt, int x, int y, int canvasW, int canvasH,
                  uint32_t& id, float& coverage)
{
    if (!opt.cryptoActive() || cryptoPath.empty() || canvasW <= 0 || canvasH <= 0) return false;
    try {
        Imf::MultiPartInputFile file(ToUtf8(cryptoPath).c_str());
        const int part = std::clamp(opt.cryptoPart, 0, file.parts() - 1);
        const Imath::Box2i disp = DisplayWindow(file.header(part));
        const int mw = disp.max.x - disp.min.x + 1, mh = disp.max.y - disp.min.y + 1;
        const int px = disp.min.x + (mw == canvasW ? x : int(int64_t(x) * mw / canvasW));
        const int py = disp.min.y + (mh == canvasH ? y : int(int64_t(y) * mh / canvasH));

        Imf::InputPart in(file, part);
        const Imath::Box2i dw = in.header().dataWindow();
        if (px < dw.min.x || px > dw.max.x || py < dw.min.y || py > dw.max.y) return false;
        const int w = dw.max.x - dw.min.x + 1;
        const int ranks = (int)opt.cryptoChannels.size() / 2;
        const size_t stride = sizeof(float) * ranks * 2;
        std::vector<float> row(size_t(w) * ranks * 2);
        // yStride 0: the single requested row lands in `row`.
        char* base = reinterpret_cast<char*>(row.data()) - ptrdiff_t(dw.min.x) * ptrdiff_t(stride);
        Imf::FrameBuffer fb;
        InsertCryptoSlices(fb, opt, base, stride, 0);
        in.setFrameBuffer(fb);
        in.readPixels(py, py);

        const float* p = row.data() + size_t(px - dw.min.x) * ranks * 2;
        coverage = 0;
        for (int r = 0; r < ranks; ++r)
            if (p[r * 2 + 1] > coverage) {
                coverage = p[r * 2 + 1];
                std::memcpy(&id, &p[r * 2], 4);
            }
        return coverage > 0;
    } catch (const std::exception& e) {
        Log("PickCryptoId failed: %s", e.what());
        return false;
    }
}
