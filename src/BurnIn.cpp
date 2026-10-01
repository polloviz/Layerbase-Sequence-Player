// Export burn-in: text rendered with GDI into a small bitmap, blended into the frame.
#include "BurnIn.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

// Pixel access for the packed 8/16-bit layouts fed to FFmpeg.
struct Pixels {
    uint8_t* data;
    int width, height, channels;
    bool sixteen;

    float get(int x, int y, int c) const
    {
        const size_t i = (size_t(y) * width + x) * channels + c;
        return sixteen ? reinterpret_cast<const uint16_t*>(data)[i] / 65535.0f : data[i] / 255.0f;
    }
    void set(int x, int y, int c, float v) const
    {
        const size_t i = (size_t(y) * width + x) * channels + c;
        v = std::clamp(v, 0.0f, 1.0f);
        if (sixteen) reinterpret_cast<uint16_t*>(data)[i] = (uint16_t)std::lround(v * 65535.0f);
        else data[i] = (uint8_t)std::lround(v * 255.0f);
    }
};

// One text box. (ax, ay) is the anchor corner; right/bottom say which corner it is.
void DrawBox(const Pixels& px, HDC dc, const std::wstring& text, int ax, int ay, bool right, bool bottom, int pad)
{
    if (text.empty()) return;
    SIZE size{};
    GetTextExtentPoint32W(dc, text.c_str(), (int)text.size(), &size);
    const int bw = size.cx + pad * 2, bh = size.cy + pad;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = bw;
    bi.bmiHeader.biHeight = -bh;   // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp || !bits) return;
    HGDIOBJ old = SelectObject(dc, bmp);
    std::memset(bits, 0, size_t(bw) * bh * 4);
    TextOutW(dc, pad, pad / 2, text.c_str(), (int)text.size());
    GdiFlush();

    const int x0 = right ? ax - bw : ax, y0 = bottom ? ay - bh : ay;
    const auto* cov = static_cast<const uint8_t*>(bits);
    for (int j = 0; j < bh; ++j) {
        const int y = y0 + j;
        if (y < 0 || y >= px.height) continue;
        for (int i = 0; i < bw; ++i) {
            const int x = x0 + i;
            if (x < 0 || x >= px.width) continue;
            const float a = cov[(size_t(j) * bw + i) * 4 + 1] / 255.0f;   // white text on black: any channel
            for (int c = 0; c < 3; ++c) px.set(x, y, c, px.get(x, y, c) * 0.4f * (1.0f - a) + a);
            if (px.channels == 4) px.set(x, y, 3, 1.0f);
        }
    }
    SelectObject(dc, old);
    DeleteObject(bmp);
}

}  // namespace

void DrawBurnIn(uint8_t* data, int width, int height, int channels, bool sixteenBit, int rx, int ry, int rw, int rh, const BurnInText& text)
{
    if (!data || rw <= 0 || rh <= 0) return;
    const int fontPx = std::clamp(std::min(rh / 30, rw / 45), 10, 160);   // narrow (vertical) frames too
    const int margin = std::max(4, fontPx * 2 / 3), pad = std::max(3, fontPx / 3);
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return;
    HFONT font = CreateFontW(-fontPx, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                             ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetTextColor(dc, RGB(255, 255, 255));
    SetBkMode(dc, TRANSPARENT);
    const Pixels px{ data, width, height, channels, sixteenBit };
    DrawBox(px, dc, text.topLeft, rx + margin, ry + margin, false, false, pad);
    DrawBox(px, dc, text.topRight, rx + rw - margin, ry + margin, true, false, pad);
    DrawBox(px, dc, text.bottomLeft, rx + margin, ry + rh - margin, false, true, pad);
    DrawBox(px, dc, text.bottomRight, rx + rw - margin, ry + rh - margin, true, true, pad);
    SelectObject(dc, oldFont);
    DeleteObject(font);
    DeleteDC(dc);
}

void FillOutside(uint8_t* data, int width, int height, int channels, bool sixteenBit, int rx, int ry, int rw, int rh)
{
    const Pixels px{ data, width, height, channels, sixteenBit };
    for (int y = 0; y < height; ++y) {
        const bool row = y < ry || y >= ry + rh;
        for (int x = 0; x < width; ++x) {
            if (!row && x >= rx && x < rx + rw) {
                x = rx + rw - 1;   // skip the inside
                continue;
            }
            for (int c = 0; c < 3; ++c) px.set(x, y, c, 0.0f);
            if (channels == 4) px.set(x, y, 3, 1.0f);
        }
    }
}

std::string FrameTimecode(int frame, double fps)
{
    const int rate = std::max(1, (int)std::lround(fps));
    const int f = std::max(0, frame);
    char buf[32];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d:%02d", f / (rate * 3600), f / (rate * 60) % 60, f / rate % 60, f % rate);
    return buf;
}
