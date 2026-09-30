#pragma once
#include "ExrLayers.h"
#include "Image.h"
#include <string>
#include <vector>

// Lowercase extensions with leading dot.
const std::vector<std::wstring>& SupportedExtensions();
bool IsSupportedExtension(const std::wstring& ext);
// True for formats that usually hold scene-linear float data (EXR, HDR).
bool IsFloatFormat(const std::wstring& ext);

void InitImageIO();   // sets OpenEXR thread pool size
// opt selects EXR layer / Cryptomatte. With an external Cryptomatte (opt->cryptoFiles)
// the mask comes from mattePath and works for every format.
ImagePtr LoadImageFile(const std::wstring& path, const LoadOptions* opt = nullptr, const std::wstring& mattePath = {});
// Writes packed RGB (8 or 16 bit per channel, top row first). The format follows the
// extension: .png and .jpg store 8 bits (16-bit data is reduced), .tif keeps 16 bits.
bool SaveImageFile(const std::wstring& path, const void* rgb, int width, int height, bool sixteenBit, std::string& err);
// 8-bit RGB as PNG file bytes; fast = light compression (clipboard).
std::string EncodePng(const uint8_t* rgb, int width, int height, bool fast = false);
// Box-filtered copy at 1/factor of the size (edge blocks average what they cover).
ImagePtr Downscale(const ImagePtr& img, int factor);
