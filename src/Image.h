#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// All decoded images are stored as interleaved RGBA.
enum class PixelType : uint8_t { U8, U16, F16 };

inline size_t BytesPerChannel(PixelType t) { return t == PixelType::U8 ? 1 : 2; }

struct Image {
    int width = 0;
    int height = 0;
    PixelType type = PixelType::U8;
    bool hasAlpha = false;
    std::vector<uint8_t> data;   // width * height * 4 * BytesPerChannel(type)
    std::string description;     // e.g. "EXR · half · PIZ"
    std::string error;           // non-empty if decoding failed

    bool valid() const { return error.empty() && width > 0 && height > 0; }
    size_t rowBytes() const { return size_t(width) * 4 * BytesPerChannel(type); }
    size_t memorySize() const { return data.size() + sizeof(Image) + description.size() + error.size(); }
};

using ImagePtr = std::shared_ptr<const Image>;

// Reads a pixel as float RGBA (integer types normalized to 0..1).
bool SamplePixel(const Image& img, int x, int y, float out[4]);
