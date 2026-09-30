#include "ImageIO.h"
#include "Platform.h"

#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <thread>

#include <ImfRgbaFile.h>
#include <ImfHeader.h>
#include <ImfChannelList.h>
#include <ImfThreading.h>
#include <ImathBox.h>
#include <half.h>

#include <tiffio.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_GIF
#define STBI_NO_PIC
#define STBI_NO_PNM
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

// ---------------------------------------------------------------------------

const std::vector<std::wstring>& SupportedExtensions()
{
    static const std::vector<std::wstring> exts = {
        L".exr", L".dpx", L".tif", L".tiff", L".png", L".jpg", L".jpeg",
        L".tga", L".bmp", L".hdr", L".psd",
    };
    return exts;
}

bool IsSupportedExtension(const std::wstring& ext)
{
    const auto& e = SupportedExtensions();
    return std::find(e.begin(), e.end(), ext) != e.end();
}

bool IsFloatFormat(const std::wstring& ext)
{
    return ext == L".exr" || ext == L".hdr";
}

void InitImageIO()
{
    Imf::setGlobalThreadCount((int)std::max(2u, std::thread::hardware_concurrency()));
}

static std::shared_ptr<Image> ErrorImage(const std::string& msg)
{
    auto img = std::make_shared<Image>();
    img->error = msg.empty() ? "Unknown error" : msg;
    return img;
}

static bool ReadWholeFile(const std::wstring& path, std::vector<uint8_t>& out)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size;
    if (!GetFileSizeEx(h, &size) || size.QuadPart > (1ll << 32)) { CloseHandle(h); return false; }
    out.resize((size_t)size.QuadPart);
    size_t done = 0;
    while (done < out.size()) {
        DWORD chunk = (DWORD)std::min<size_t>(out.size() - done, 64u << 20), got = 0;
        if (!ReadFile(h, out.data() + done, chunk, &got, nullptr) || got == 0) break;
        done += got;
    }
    CloseHandle(h);
    return done == out.size();
}

// ---------------------------------------------------------------------------
// OpenEXR

static const char* CompressionName(Imf::Compression c)
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

static ImagePtr LoadEXR(const std::wstring& path)
{
    try {
        Imf::RgbaInputFile file(ToUtf8(path).c_str());
        const Imath::Box2i dw = file.dataWindow();
        Imath::Box2i disp = file.displayWindow();
        if (disp.isEmpty()) disp = dw;

        auto img = std::make_shared<Image>();
        img->width = disp.max.x - disp.min.x + 1;
        img->height = disp.max.y - disp.min.y + 1;
        img->type = PixelType::F16;
        img->hasAlpha = (file.channels() & Imf::WRITE_A) != 0;
        img->data.resize(size_t(img->width) * img->height * sizeof(Imf::Rgba));

        const Imf::ChannelList& ch = file.header().channels();
        const char* ptype = "half";
        if (ch.begin() != ch.end() && ch.begin().channel().type == Imf::FLOAT) ptype = "float";
        img->description = std::string("EXR · ") + ptype + " · " + CompressionName(file.compression());

        Imf::Rgba* canvas = reinterpret_cast<Imf::Rgba*>(img->data.data());
        const bool contained = dw.min.x >= disp.min.x && dw.min.y >= disp.min.y &&
                               dw.max.x <= disp.max.x && dw.max.y <= disp.max.y;
        if (contained) {
            // Data window inside display window: decode straight into the canvas.
            if (dw != disp) std::memset(img->data.data(), 0, img->data.size());
            file.setFrameBuffer(canvas - disp.min.x - ptrdiff_t(disp.min.y) * img->width, 1, img->width);
            file.readPixels(dw.min.y, dw.max.y);
        } else {
            // Overscan or disjoint windows: decode data window, then crop.
            const int dwW = dw.max.x - dw.min.x + 1, dwH = dw.max.y - dw.min.y + 1;
            std::vector<Imf::Rgba> tmp(size_t(dwW) * dwH);
            file.setFrameBuffer(tmp.data() - dw.min.x - ptrdiff_t(dw.min.y) * dwW, 1, dwW);
            file.readPixels(dw.min.y, dw.max.y);
            std::memset(img->data.data(), 0, img->data.size());
            const int x0 = std::max(dw.min.x, disp.min.x), x1 = std::min(dw.max.x, disp.max.x);
            const int y0 = std::max(dw.min.y, disp.min.y), y1 = std::min(dw.max.y, disp.max.y);
            for (int y = y0; y <= y1 && x0 <= x1; ++y)
                std::memcpy(canvas + size_t(y - disp.min.y) * img->width + (x0 - disp.min.x),
                            tmp.data() + size_t(y - dw.min.y) * dwW + (x0 - dw.min.x),
                            size_t(x1 - x0 + 1) * sizeof(Imf::Rgba));
        }
        return img;
    } catch (const std::exception& e) {
        return ErrorImage(e.what());
    }
}

// ---------------------------------------------------------------------------
// stb_image (PNG, JPEG, TGA, BMP, HDR, PSD)

static ImagePtr LoadSTB(const std::wstring& path, const std::wstring& ext)
{
    std::vector<uint8_t> buf;
    if (!ReadWholeFile(path, buf)) return ErrorImage("Cannot read file");
    auto img = std::make_shared<Image>();
    int w = 0, h = 0, comp = 0;
    const int len = (int)buf.size();
    std::string fmt = ToUtf8(ext.substr(1));
    for (auto& c : fmt) c = (char)toupper(c);

    if (stbi_is_hdr_from_memory(buf.data(), len)) {
        float* px = stbi_loadf_from_memory(buf.data(), len, &w, &h, &comp, 4);
        if (!px) return ErrorImage(stbi_failure_reason() ? stbi_failure_reason() : "Decode failed");
        img->type = PixelType::F16;
        img->data.resize(size_t(w) * h * 4 * 2);
        half* dst = reinterpret_cast<half*>(img->data.data());
        for (size_t i = 0, n = size_t(w) * h * 4; i < n; ++i) dst[i] = half(px[i]);
        stbi_image_free(px);
        img->description = fmt + " · float";
    } else if (stbi_is_16_bit_from_memory(buf.data(), len)) {
        stbi_us* px = stbi_load_16_from_memory(buf.data(), len, &w, &h, &comp, 4);
        if (!px) return ErrorImage(stbi_failure_reason() ? stbi_failure_reason() : "Decode failed");
        img->type = PixelType::U16;
        img->data.assign((uint8_t*)px, (uint8_t*)px + size_t(w) * h * 8);
        stbi_image_free(px);
        img->description = fmt + " · 16-bit";
    } else {
        stbi_uc* px = stbi_load_from_memory(buf.data(), len, &w, &h, &comp, 4);
        if (!px) return ErrorImage(stbi_failure_reason() ? stbi_failure_reason() : "Decode failed");
        img->type = PixelType::U8;
        img->data.assign(px, px + size_t(w) * h * 4);
        stbi_image_free(px);
        img->description = fmt + " · 8-bit";
    }
    img->width = w;
    img->height = h;
    img->hasAlpha = (comp == 2 || comp == 4);
    return img;
}

// ---------------------------------------------------------------------------
// TIFF

static void TiffSilentHandler(const char*, const char*, va_list) {}

static ImagePtr LoadTIFF(const std::wstring& path)
{
    static bool init = [] { TIFFSetWarningHandler(TiffSilentHandler); TIFFSetErrorHandler(TiffSilentHandler); return true; }();
    (void)init;

    TIFF* tif = TIFFOpenW(path.c_str(), "r");
    if (!tif) return ErrorImage("Cannot open TIFF");

    uint32_t w = 0, h = 0;
    uint16_t spp = 1, bps = 8, fmt = SAMPLEFORMAT_UINT, planar = PLANARCONFIG_CONTIG, photo = PHOTOMETRIC_RGB;
    TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &w);
    TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &h);
    TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLESPERPIXEL, &spp);
    TIFFGetFieldDefaulted(tif, TIFFTAG_BITSPERSAMPLE, &bps);
    TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLEFORMAT, &fmt);
    TIFFGetFieldDefaulted(tif, TIFFTAG_PLANARCONFIG, &planar);
    TIFFGetFieldDefaulted(tif, TIFFTAG_PHOTOMETRIC, &photo);

    auto img = std::make_shared<Image>();
    img->width = (int)w;
    img->height = (int)h;

    const bool simple = planar == PLANARCONFIG_CONTIG &&
        (photo == PHOTOMETRIC_RGB || photo == PHOTOMETRIC_MINISBLACK) &&
        spp >= 1 && spp <= 4 &&
        ((fmt == SAMPLEFORMAT_UINT && (bps == 8 || bps == 16)) ||
         (fmt == SAMPLEFORMAT_IEEEFP && (bps == 16 || bps == 32)));

    if (!simple) {
        // Generic path (palette, YCbCr, CMYK, 1-bit...) -> 8-bit RGBA.
        img->type = PixelType::U8;
        img->data.resize(size_t(w) * h * 4);
        if (!TIFFReadRGBAImageOriented(tif, w, h, reinterpret_cast<uint32_t*>(img->data.data()), ORIENTATION_TOPLEFT, 0)) {
            TIFFClose(tif);
            return ErrorImage("Unsupported TIFF layout");
        }
        img->hasAlpha = spp == 4 || spp == 2;
        img->description = "TIFF · 8-bit";
        TIFFClose(tif);
        return img;
    }

    // Read raw interleaved samples (strips or tiles) into one buffer.
    const size_t bytesPerSample = bps / 8;
    const size_t rawRow = size_t(w) * spp * bytesPerSample;
    std::vector<uint8_t> raw(rawRow * h);
    bool ok = true;
    if (TIFFIsTiled(tif)) {
        uint32_t tw = 0, th = 0;
        TIFFGetField(tif, TIFFTAG_TILEWIDTH, &tw);
        TIFFGetField(tif, TIFFTAG_TILELENGTH, &th);
        std::vector<uint8_t> tile(TIFFTileSize(tif));
        const size_t tileRow = size_t(tw) * spp * bytesPerSample;
        for (uint32_t ty = 0; ty < h && ok; ty += th)
            for (uint32_t tx = 0; tx < w && ok; tx += tw) {
                if (TIFFReadTile(tif, tile.data(), tx, ty, 0, 0) < 0) { ok = false; break; }
                const uint32_t cw = std::min(tw, w - tx), chh = std::min(th, h - ty);
                for (uint32_t r = 0; r < chh; ++r)
                    std::memcpy(raw.data() + (ty + r) * rawRow + size_t(tx) * spp * bytesPerSample,
                                tile.data() + r * tileRow, size_t(cw) * spp * bytesPerSample);
            }
    } else {
        for (uint32_t y = 0; y < h && ok; ++y)
            ok = TIFFReadScanline(tif, raw.data() + y * rawRow, y) >= 0;
    }
    // Declared alpha kind (libtiff's generic RGBA path above always returns it premultiplied).
    if (spp == 2 || spp == 4) {
        uint16_t extraCount = 0;
        uint16_t* extra = nullptr;
        if (TIFFGetField(tif, TIFFTAG_EXTRASAMPLES, &extraCount, &extra) && extraCount > 0 && extra)
            img->straightAlpha = extra[0] == EXTRASAMPLE_UNASSALPHA;
    }
    TIFFClose(tif);
    if (!ok) return ErrorImage("TIFF decode error");

    const bool gray = spp <= 2;
    img->hasAlpha = spp == 2 || spp == 4;
    const size_t n = size_t(w) * h;
    auto expand = [&](auto* dst, const auto* src, auto one) {
        for (size_t i = 0; i < n; ++i, src += spp, dst += 4) {
            if (gray) { dst[0] = dst[1] = dst[2] = src[0]; dst[3] = spp == 2 ? src[1] : one; }
            else { dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; dst[3] = spp == 4 ? src[3] : one; }
        }
    };

    if (fmt == SAMPLEFORMAT_UINT && bps == 8) {
        img->type = PixelType::U8;
        img->data.resize(n * 4);
        expand(img->data.data(), raw.data(), uint8_t(255));
        img->description = "TIFF · 8-bit";
    } else if (fmt == SAMPLEFORMAT_UINT && bps == 16) {
        img->type = PixelType::U16;
        img->data.resize(n * 8);
        expand(reinterpret_cast<uint16_t*>(img->data.data()), reinterpret_cast<const uint16_t*>(raw.data()), uint16_t(65535));
        img->description = "TIFF · 16-bit";
    } else if (bps == 16) {
        img->type = PixelType::F16;
        img->data.resize(n * 8);
        expand(reinterpret_cast<uint16_t*>(img->data.data()), reinterpret_cast<const uint16_t*>(raw.data()),
               half(1.0f).bits());
        img->description = "TIFF · half";
    } else {
        img->type = PixelType::F16;
        img->data.resize(n * 8);
        const float* src = reinterpret_cast<const float*>(raw.data());
        half* dst = reinterpret_cast<half*>(img->data.data());
        for (size_t i = 0; i < n; ++i, src += spp, dst += 4) {
            if (gray) { dst[0] = dst[1] = dst[2] = half(src[0]); dst[3] = half(spp == 2 ? src[1] : 1.0f); }
            else { dst[0] = half(src[0]); dst[1] = half(src[1]); dst[2] = half(src[2]); dst[3] = half(spp == 4 ? src[3] : 1.0f); }
        }
        img->description = "TIFF · float";
    }
    return img;
}

// ---------------------------------------------------------------------------
// DPX (8/10/12/16-bit, RGB/RGBA/Luma, filled method A/B)

static ImagePtr LoadDPX(const std::wstring& path)
{
    std::vector<uint8_t> buf;
    if (!ReadWholeFile(path, buf) || buf.size() < 2048) return ErrorImage("Cannot read DPX");
    const uint8_t* p = buf.data();
    uint32_t magic;
    std::memcpy(&magic, p, 4);
    bool swap;
    if (magic == 0x58504453) swap = true;         // "SDPX" read little-endian -> big-endian file
    else if (magic == 0x53445058) swap = false;   // "XPDS" -> little-endian file
    else return ErrorImage("Not a DPX file");

    auto u32 = [&](size_t off) { uint32_t v; std::memcpy(&v, p + off, 4); return swap ? _byteswap_ulong(v) : v; };
    auto u16 = [&](size_t off) { uint16_t v; std::memcpy(&v, p + off, 2); return swap ? _byteswap_ushort(v) : v; };

    const uint32_t imageOffset = u32(4);
    const uint32_t w = u32(772), h = u32(776);
    const uint8_t descriptor = p[800];
    const uint8_t bits = p[803];
    const uint16_t packing = u16(804);
    const uint16_t encoding = u16(806);
    uint32_t dataOffset = u32(808);
    uint32_t eolPad = u32(812);
    if (dataOffset == 0 || dataOffset == 0xFFFFFFFF) dataOffset = imageOffset;
    if (eolPad == 0xFFFFFFFF) eolPad = 0;
    if (encoding != 0) return ErrorImage("RLE DPX not supported");
    if (w == 0 || h == 0 || w > 65536 || h > 65536) return ErrorImage("Invalid DPX size");

    int c;
    switch (descriptor) {
    case 6: c = 1; break;          // luma
    case 50: c = 3; break;         // RGB
    case 51: case 52: c = 4; break; // RGBA / ABGR
    default: return ErrorImage("Unsupported DPX descriptor");
    }

    const size_t samplesPerRow = size_t(w) * c;
    size_t rowBytes;
    switch (bits) {
    case 8:  rowBytes = (samplesPerRow + 3) / 4 * 4; break;
    case 10: rowBytes = (packing == 0 ? (samplesPerRow * 10 + 31) / 32 : (samplesPerRow + 2) / 3) * 4; break;
    case 12: rowBytes = (packing == 0 ? (samplesPerRow * 12 + 31) / 32 * 4 : (samplesPerRow * 2 + 3) / 4 * 4); break;
    case 16: rowBytes = (samplesPerRow * 2 + 3) / 4 * 4; break;
    default: return ErrorImage("Unsupported DPX bit depth");
    }
    rowBytes += eolPad;
    if (size_t(dataOffset) + rowBytes * (h - 1) + samplesPerRow > buf.size()) return ErrorImage("Truncated DPX");

    auto img = std::make_shared<Image>();
    img->width = (int)w;
    img->height = (int)h;
    img->hasAlpha = c == 4;
    img->type = bits == 8 ? PixelType::U8 : PixelType::U16;
    img->data.resize(size_t(w) * h * 4 * BytesPerChannel(img->type));
    img->description = "DPX · " + std::to_string(bits) + "-bit";

    std::vector<uint16_t> row(samplesPerRow + 3);
    for (uint32_t y = 0; y < h; ++y) {
        const uint8_t* src = p + dataOffset + rowBytes * y;
        // Decode one row into 16-bit samples (full range 0..65535).
        if (bits == 8) {
            for (size_t i = 0; i < samplesPerRow; ++i) row[i] = uint16_t(src[i] * 257);
        } else if (bits == 16) {
            for (size_t i = 0; i < samplesPerRow; ++i) {
                uint16_t v; std::memcpy(&v, src + i * 2, 2);
                row[i] = swap ? _byteswap_ushort(v) : v;
            }
        } else if (bits == 10 && packing != 0) {
            const int s0 = packing == 1 ? 22 : 20, s1 = packing == 1 ? 12 : 10, s2 = packing == 1 ? 2 : 0;
            for (size_t i = 0, word = 0; i < samplesPerRow; i += 3, ++word) {
                uint32_t v; std::memcpy(&v, src + word * 4, 4);
                if (swap) v = _byteswap_ulong(v);
                const uint32_t a = (v >> s0) & 0x3FF, b = (v >> s1) & 0x3FF, d = (v >> s2) & 0x3FF;
                row[i] = uint16_t(a << 6 | a >> 4);
                row[i + 1] = uint16_t(b << 6 | b >> 4);
                row[i + 2] = uint16_t(d << 6 | d >> 4);
            }
        } else if (bits == 12 && packing != 0) {
            for (size_t i = 0; i < samplesPerRow; ++i) {
                uint16_t v; std::memcpy(&v, src + i * 2, 2);
                if (swap) v = _byteswap_ushort(v);
                uint16_t s = packing == 1 ? uint16_t(v >> 4) : uint16_t(v & 0xFFF);
                row[i] = uint16_t(s << 4 | s >> 8);
            }
        } else {
            // Packed 10/12-bit: continuous bitstream in 32-bit words, LSB first.
            size_t bitPos = 0;
            for (size_t i = 0; i < samplesPerRow; ++i, bitPos += bits) {
                const size_t word = bitPos / 32, shift = bitPos % 32;
                uint32_t v0; std::memcpy(&v0, src + word * 4, 4);
                if (swap) v0 = _byteswap_ulong(v0);
                uint64_t v = v0;
                if (shift + bits > 32) {
                    uint32_t v1; std::memcpy(&v1, src + (word + 1) * 4, 4);
                    if (swap) v1 = _byteswap_ulong(v1);
                    v |= uint64_t(v1) << 32;
                }
                const uint32_t s = uint32_t(v >> shift) & ((1u << bits) - 1);
                row[i] = uint16_t(s << (16 - bits) | s >> (2 * bits - 16));
            }
        }

        for (uint32_t x = 0; x < w; ++x) {
            const uint16_t* s = &row[size_t(x) * c];
            uint16_t r, g, b, a = 65535;
            if (c == 1) r = g = b = s[0];
            else if (descriptor == 52) { a = s[0]; b = s[1]; g = s[2]; r = s[3]; }
            else { r = s[0]; g = s[1]; b = s[2]; if (c == 4) a = s[3]; }
            const size_t o = (size_t(y) * w + x) * 4;
            if (img->type == PixelType::U8) {
                uint8_t* d = img->data.data() + o;
                d[0] = uint8_t(r >> 8); d[1] = uint8_t(g >> 8); d[2] = uint8_t(b >> 8); d[3] = uint8_t(a >> 8);
            } else {
                uint16_t* d = reinterpret_cast<uint16_t*>(img->data.data()) + o;
                d[0] = r; d[1] = g; d[2] = b; d[3] = a;
            }
        }
    }
    return img;
}

// ---------------------------------------------------------------------------

ImagePtr LoadImageFile(const std::wstring& path, const LoadOptions* opt, const std::wstring& mattePath)
{
    const std::wstring ext = GetFileExtension(path);
    try {
        if (opt && opt->cryptoExternal()) {
            LoadOptions layer;   // the frame's own layer, then the external mask
            layer.part = opt->part;
            layer.channels = opt->channels;
            ImagePtr img = LoadImageFile(path, &layer);
            return img->valid() ? ApplyExternalCrypto(img, mattePath, *opt) : img;
        }
        if (ext == L".exr") return opt && !opt->isDefault() ? LoadExrWithOptions(path, *opt) : LoadEXR(path);
        if (ext == L".dpx") return LoadDPX(path);
        if (ext == L".tif" || ext == L".tiff") return LoadTIFF(path);
        return LoadSTB(path, ext);
    } catch (const std::bad_alloc&) {
        return ErrorImage("Out of memory");
    } catch (const std::exception& e) {
        return ErrorImage(e.what());
    }
}

namespace {

template <class T, class ToFloat, class FromFloat>
void BoxDownscale(const Image& src, Image& dst, int factor, ToFloat toFloat, FromFloat fromFloat)
{
    const T* in = reinterpret_cast<const T*>(src.data.data());
    T* out = reinterpret_cast<T*>(dst.data.data());
    const size_t rowIn = size_t(src.width) * 4, rowOut = size_t(dst.width) * 4;
    std::vector<float> acc(rowOut);
    for (int oy = 0; oy < dst.height; ++oy) {
        std::fill(acc.begin(), acc.end(), 0.0f);
        const int y0 = oy * factor, y1 = std::min(src.height, y0 + factor);
        for (int y = y0; y < y1; ++y) {
            const T* p = in + size_t(y) * rowIn;
            for (int ox = 0; ox < dst.width; ++ox) {
                float* a = &acc[size_t(ox) * 4];
                for (int x = ox * factor, x1 = std::min(src.width, x + factor); x < x1; ++x, p += 4) {
                    a[0] += toFloat(p[0]); a[1] += toFloat(p[1]); a[2] += toFloat(p[2]); a[3] += toFloat(p[3]);
                }
            }
        }
        T* q = out + size_t(oy) * rowOut;
        for (int ox = 0; ox < dst.width; ++ox) {
            const float n = float((y1 - y0) * (std::min(src.width, (ox + 1) * factor) - ox * factor));
            for (int c = 0; c < 4; ++c) q[ox * 4 + c] = fromFloat(acc[size_t(ox) * 4 + c] / n);
        }
    }
}

}  // namespace

ImagePtr Downscale(const ImagePtr& img, int factor)
{
    if (!img || !img->valid() || factor <= 1) return img;
    auto out = std::make_shared<Image>();
    out->width = (img->width + factor - 1) / factor;
    out->height = (img->height + factor - 1) / factor;
    out->type = img->type;
    out->hasAlpha = img->hasAlpha;
    out->straightAlpha = img->straightAlpha;
    out->description = img->description;
    out->fullW = img->fullWidth();
    out->fullH = img->fullHeight();
    out->data.resize(size_t(out->width) * out->height * 4 * BytesPerChannel(img->type));
    switch (img->type) {
    case PixelType::U8:
        BoxDownscale<uint8_t>(*img, *out, factor, [](uint8_t v) { return float(v); },
                              [](float v) { return uint8_t(v + 0.5f); });
        break;
    case PixelType::U16:
        BoxDownscale<uint16_t>(*img, *out, factor, [](uint16_t v) { return float(v); },
                               [](float v) { return uint16_t(v + 0.5f); });
        break;
    case PixelType::F16:
        BoxDownscale<half>(*img, *out, factor, [](half v) { return float(v); }, [](float v) { return half(v); });
        break;
    }
    return out;
}

bool SamplePixel(const Image& img, int x, int y, float out[4])
{
    if (!img.valid() || x < 0 || y < 0 || x >= img.width || y >= img.height) return false;
    const size_t o = (size_t(y) * img.width + x) * 4;
    switch (img.type) {
    case PixelType::U8:
        for (int i = 0; i < 4; ++i) out[i] = img.data[o + i] / 255.0f;
        break;
    case PixelType::U16: {
        const uint16_t* d = reinterpret_cast<const uint16_t*>(img.data.data()) + o;
        for (int i = 0; i < 4; ++i) out[i] = d[i] / 65535.0f;
        break;
    }
    case PixelType::F16: {
        const half* d = reinterpret_cast<const half*>(img.data.data()) + o;
        for (int i = 0; i < 4; ++i) out[i] = float(d[i]);
        break;
    }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Writing

static void AppendBytes(void* ctx, void* data, int size)
{
    static_cast<std::string*>(ctx)->append(static_cast<const char*>(data), size);
}

std::string EncodePng(const uint8_t* rgb, int width, int height, bool fast)
{
    std::string out;
    stbi_write_png_compression_level = fast ? 1 : 8;
    stbi_write_png_to_func(AppendBytes, &out, width, height, 3, rgb, width * 3);
    return out;
}

bool SaveImageFile(const std::wstring& path, const void* rgb, int width, int height, bool sixteenBit, std::string& err)
{
    const std::wstring ext = GetFileExtension(path);
    const size_t count = size_t(width) * height * 3;
    if (ext == L".tif" || ext == L".tiff") {
        TIFF* tif = TIFFOpenW(path.c_str(), "w");
        if (!tif) { err = "Cannot create the file"; return false; }
        TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, uint32_t(width));
        TIFFSetField(tif, TIFFTAG_IMAGELENGTH, uint32_t(height));
        TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, uint16_t(3));
        TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, uint16_t(sixteenBit ? 16 : 8));
        TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
        TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
        TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_ADOBE_DEFLATE);
        TIFFSetField(tif, TIFFTAG_PREDICTOR, PREDICTOR_HORIZONTAL);
        TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, TIFFDefaultStripSize(tif, 0));
        const size_t row = size_t(width) * 3 * (sixteenBit ? 2 : 1);
        bool ok = true;
        for (int y = 0; y < height && ok; ++y)
            ok = TIFFWriteScanline(tif, const_cast<uint8_t*>(static_cast<const uint8_t*>(rgb) + row * y), uint32_t(y)) >= 0;
        TIFFClose(tif);
        if (!ok) err = "Cannot write the file";
        return ok;
    }
    std::vector<uint8_t> eight;
    const uint8_t* px = static_cast<const uint8_t*>(rgb);
    if (sixteenBit) {
        eight.resize(count);
        const uint16_t* w = static_cast<const uint16_t*>(rgb);
        for (size_t i = 0; i < count; ++i) eight[i] = uint8_t((w[i] + 128) / 257);
        px = eight.data();
    }
    std::string bytes;
    if (ext == L".png") bytes = EncodePng(px, width, height);
    else if (ext == L".jpg" || ext == L".jpeg") stbi_write_jpg_to_func(AppendBytes, &bytes, width, height, 3, px, 95);
    else { err = "Unsupported format (use .png, .jpg or .tif)"; return false; }
    if (bytes.empty()) { err = "Cannot encode the image"; return false; }
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) { err = "Cannot create the file"; return false; }
    const bool ok = fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    fclose(f);
    if (!ok) err = "Cannot write the file";
    return ok;
}
