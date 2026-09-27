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
// Box-filtered copy at 1/factor of the size (edge blocks average what they cover).
ImagePtr Downscale(const ImagePtr& img, int factor);
