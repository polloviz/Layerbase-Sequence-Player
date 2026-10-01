#include "Metadata.h"
#include "Platform.h"

#include <windows.h>

#include <ImfBoxAttribute.h>
#include <ImfChannelListAttribute.h>
#include <ImfChromaticitiesAttribute.h>
#include <ImfCompressionAttribute.h>
#include <ImfDoubleAttribute.h>
#include <ImfEnvmapAttribute.h>
#include <ImfFloatAttribute.h>
#include <ImfFloatVectorAttribute.h>
#include <ImfHeader.h>
#include <ImfIntAttribute.h>
#include <ImfKeyCodeAttribute.h>
#include <ImfLineOrderAttribute.h>
#include <ImfMatrixAttribute.h>
#include <ImfMultiPartInputFile.h>
#include <ImfPreviewImageAttribute.h>
#include <ImfRationalAttribute.h>
#include <ImfStringAttribute.h>
#include <ImfStringVectorAttribute.h>
#include <ImfTileDescriptionAttribute.h>
#include <ImfTimeCodeAttribute.h>
#include <ImfVecAttribute.h>

#include <tiffio.h>

#include <cstdio>

namespace {

std::string Num(double v)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%.6g", v);
    return buf;
}

template <class V>
std::string Vec(const V* v, int n)
{
    std::string s = "(";
    for (int i = 0; i < n; ++i) s += (i ? ", " : "") + Num((*v)[i]);
    return s + ")";
}

template <class M>
std::string Rows(const M& m, int n)
{
    std::string s;
    for (int r = 0; r < n; ++r) {
        s += r ? "  (" : "(";
        for (int c = 0; c < n; ++c) s += (c ? ", " : "") + Num(m[r][c]);
        s += ")";
    }
    return s;
}

const char* CompressionText(Imf::Compression c)
{
    switch (c) {
    case Imf::NO_COMPRESSION: return "none";
    case Imf::RLE_COMPRESSION: return "RLE";
    case Imf::ZIPS_COMPRESSION: return "ZIPS";
    case Imf::ZIP_COMPRESSION: return "ZIP";
    case Imf::PIZ_COMPRESSION: return "PIZ";
    case Imf::PXR24_COMPRESSION: return "PXR24";
    case Imf::B44_COMPRESSION: return "B44";
    case Imf::B44A_COMPRESSION: return "B44A";
    case Imf::DWAA_COMPRESSION: return "DWAA";
    case Imf::DWAB_COMPRESSION: return "DWAB";
    default: return "?";
    }
}

std::string AttributeText(const Imf::Attribute& a)
{
    using namespace Imf;
    if (auto* v = dynamic_cast<const StringAttribute*>(&a)) return v->value();
    if (auto* v = dynamic_cast<const IntAttribute*>(&a)) return std::to_string(v->value());
    if (auto* v = dynamic_cast<const FloatAttribute*>(&a)) return Num(v->value());
    if (auto* v = dynamic_cast<const DoubleAttribute*>(&a)) return Num(v->value());
    if (auto* v = dynamic_cast<const V2iAttribute*>(&a)) return Vec(&v->value(), 2);
    if (auto* v = dynamic_cast<const V2fAttribute*>(&a)) return Vec(&v->value(), 2);
    if (auto* v = dynamic_cast<const V2dAttribute*>(&a)) return Vec(&v->value(), 2);
    if (auto* v = dynamic_cast<const V3iAttribute*>(&a)) return Vec(&v->value(), 3);
    if (auto* v = dynamic_cast<const V3fAttribute*>(&a)) return Vec(&v->value(), 3);
    if (auto* v = dynamic_cast<const V3dAttribute*>(&a)) return Vec(&v->value(), 3);
    if (auto* v = dynamic_cast<const Box2iAttribute*>(&a)) {
        const auto& b = v->value();
        return "(" + std::to_string(b.min.x) + ", " + std::to_string(b.min.y) + ") \xE2\x80\x93 (" + std::to_string(b.max.x) + ", " +
               std::to_string(b.max.y) + ")  \xC2\xB7  " + std::to_string(b.max.x - b.min.x + 1) + "\xC3\x97" +
               std::to_string(b.max.y - b.min.y + 1);
    }
    if (auto* v = dynamic_cast<const Box2fAttribute*>(&a)) {
        const auto& b = v->value();
        return Vec(&b.min, 2) + " \xE2\x80\x93 " + Vec(&b.max, 2);
    }
    if (auto* v = dynamic_cast<const M33fAttribute*>(&a)) return Rows(v->value(), 3);
    if (auto* v = dynamic_cast<const M44fAttribute*>(&a)) return Rows(v->value(), 4);
    if (auto* v = dynamic_cast<const M44dAttribute*>(&a)) return Rows(v->value(), 4);
    if (auto* v = dynamic_cast<const ChannelListAttribute*>(&a)) {
        std::string s;
        int n = 0;
        for (auto it = v->value().begin(); it != v->value().end(); ++it, ++n) {
            const char* t = it.channel().type == Imf::HALF ? "half" : it.channel().type == Imf::FLOAT ? "float" : "uint";
            s += std::string(n ? ", " : "") + it.name() + " (" + t + ")";
        }
        return std::to_string(n) + ":  " + s;
    }
    if (auto* v = dynamic_cast<const CompressionAttribute*>(&a)) return CompressionText(v->value());
    if (auto* v = dynamic_cast<const LineOrderAttribute*>(&a))
        return v->value() == INCREASING_Y ? "increasing Y" : v->value() == DECREASING_Y ? "decreasing Y" : "random Y";
    if (auto* v = dynamic_cast<const StringVectorAttribute*>(&a)) {
        std::string s;
        for (const auto& e : v->value()) s += (s.empty() ? "" : "; ") + e;
        return s;
    }
    if (auto* v = dynamic_cast<const FloatVectorAttribute*>(&a)) {
        std::string s;
        for (float f : v->value()) s += (s.empty() ? "" : ", ") + Num(f);
        return s;
    }
    if (auto* v = dynamic_cast<const TimeCodeAttribute*>(&a)) {
        const TimeCode& t = v->value();
        char buf[32];
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d:%02d", t.hours(), t.minutes(), t.seconds(), t.frame());
        return buf;
    }
    if (auto* v = dynamic_cast<const KeyCodeAttribute*>(&a)) {
        const KeyCode& k = v->value();
        return "mfc " + std::to_string(k.filmMfcCode()) + ", type " + std::to_string(k.filmType()) + ", prefix " +
               std::to_string(k.prefix()) + ", count " + std::to_string(k.count()) + ", perf " + std::to_string(k.perfOffset());
    }
    if (auto* v = dynamic_cast<const RationalAttribute*>(&a)) {
        const Rational& r = v->value();
        return std::to_string(r.n) + "/" + std::to_string(r.d) + (r.d ? "  (" + Num(double(r)) + ")" : "");
    }
    if (auto* v = dynamic_cast<const ChromaticitiesAttribute*>(&a)) {
        const Chromaticities& c = v->value();
        return "R " + Vec(&c.red, 2) + "  G " + Vec(&c.green, 2) + "  B " + Vec(&c.blue, 2) + "  W " + Vec(&c.white, 2);
    }
    if (auto* v = dynamic_cast<const PreviewImageAttribute*>(&a))
        return std::to_string(v->value().width()) + "\xC3\x97" + std::to_string(v->value().height());
    if (auto* v = dynamic_cast<const TileDescriptionAttribute*>(&a))
        return std::to_string(v->value().xSize) + "\xC3\x97" + std::to_string(v->value().ySize) + " tiles";
    if (auto* v = dynamic_cast<const EnvmapAttribute*>(&a)) return v->value() == ENVMAP_LATLONG ? "lat-long" : "cube";
    return std::string("(") + a.typeName() + ")";
}

void ReadExr(const std::wstring& path, std::vector<MetaEntry>& out)
{
    try {
        Imf::MultiPartInputFile file(ToUtf8(path).c_str());
        for (int p = 0; p < file.parts(); ++p) {
            const Imf::Header& h = file.header(p);
            std::string title = "EXR";
            if (file.parts() > 1) title += " \xC2\xB7 part " + std::to_string(p) + (h.hasName() ? " \xC2\xB7 " + h.name() : std::string());
            out.push_back({ title, {}, true });
            for (auto it = h.begin(); it != h.end(); ++it) out.push_back({ it.name(), AttributeText(it.attribute()) });
        }
    } catch (const std::exception& e) {
        out.push_back({ "EXR", e.what() });
    }
}

void ReadTiff(const std::wstring& path, std::vector<MetaEntry>& out)
{
    TIFFSetWarningHandler(nullptr);
    TIFF* t = TIFFOpenW(path.c_str(), "r");
    if (!t) return;
    out.push_back({ "TIFF", {}, true });
    uint32_t w = 0, h = 0;
    uint16_t bits = 0, spp = 0, comp = 0, fmt = SAMPLEFORMAT_UINT;
    TIFFGetField(t, TIFFTAG_IMAGEWIDTH, &w);
    TIFFGetField(t, TIFFTAG_IMAGELENGTH, &h);
    TIFFGetFieldDefaulted(t, TIFFTAG_BITSPERSAMPLE, &bits);
    TIFFGetFieldDefaulted(t, TIFFTAG_SAMPLESPERPIXEL, &spp);
    TIFFGetFieldDefaulted(t, TIFFTAG_COMPRESSION, &comp);
    TIFFGetFieldDefaulted(t, TIFFTAG_SAMPLEFORMAT, &fmt);
    out.push_back({ "size", std::to_string(w) + "\xC3\x97" + std::to_string(h) });
    out.push_back({ "samples", std::to_string(spp) + " \xC3\x97 " + std::to_string(bits) + " bit" + (fmt == SAMPLEFORMAT_IEEEFP ? " float" : "") });
    out.push_back({ "compression", std::to_string(comp) });
    static const struct { uint32_t tag; const char* name; } tags[] = {
        { TIFFTAG_SOFTWARE, "software" }, { TIFFTAG_ARTIST, "artist" }, { TIFFTAG_DATETIME, "date" },
        { TIFFTAG_IMAGEDESCRIPTION, "description" }, { TIFFTAG_HOSTCOMPUTER, "host" }, { TIFFTAG_COPYRIGHT, "copyright" },
        { TIFFTAG_DOCUMENTNAME, "document" }, { TIFFTAG_MAKE, "make" }, { TIFFTAG_MODEL, "model" },
    };
    for (const auto& tag : tags) {
        const char* v = nullptr;
        if (TIFFGetField(t, tag.tag, &v) && v && *v) out.push_back({ tag.name, v });
    }
    TIFFClose(t);
}

std::string FormatSize(uint64_t bytes)
{
    char buf[64];
    if (bytes >= (1ull << 30)) snprintf(buf, sizeof(buf), "%.2f GB", bytes / 1073741824.0);
    else if (bytes >= (1ull << 20)) snprintf(buf, sizeof(buf), "%.2f MB", bytes / 1048576.0);
    else snprintf(buf, sizeof(buf), "%.1f KB", bytes / 1024.0);
    return buf;
}

}  // namespace

std::vector<MetaEntry> ReadMetadata(const std::wstring& path)
{
    std::vector<MetaEntry> out;
    out.push_back({ "File", {}, true });
    out.push_back({ "name", ToUtf8(GetFileName(path)) });
    out.push_back({ "folder", ToUtf8(GetParentDir(path)) });
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a)) {
        out.push_back({ "size", FormatSize((uint64_t(a.nFileSizeHigh) << 32) | a.nFileSizeLow) });
        FILETIME local;
        SYSTEMTIME st;
        if (FileTimeToLocalFileTime(&a.ftLastWriteTime, &local) && FileTimeToSystemTime(&local, &st)) {
            char buf[64];
            snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
            out.push_back({ "modified", buf });
        }
    } else {
        out.push_back({ "size", "-" });
    }
    const std::wstring ext = GetFileExtension(path);
    if (ext == L".exr") ReadExr(path, out);
    else if (ext == L".tif" || ext == L".tiff") ReadTiff(path, out);
    return out;
}
