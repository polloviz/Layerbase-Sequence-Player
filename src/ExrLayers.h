#pragma once
#include "Image.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// A viewable EXR layer: a set of channels within one part ("diffuse.R/G/B", root "R/G/B/A", "Z"...).
struct ExrLayer {
    int part = 0;
    std::string label;                 // UI name ("RGBA", "diffuse", "beauty.specular", "Z")
    std::vector<std::string> channels; // full channel names to load (mapped to RGBA at load time)
    bool isRootColor = false;          // root R/G/B(/A) of part 0: fast default path
};

// A Cryptomatte layer (spec: https://github.com/Psyop/Cryptomatte).
struct CryptoLayer {
    int part = 0;
    std::string name;                  // "CryptoObject"
    // Channel names as spelled in the file, 4 per group (id, coverage, id, coverage):
    // "CryptoObject00.R/G/B/A", "CryptoObject01.R/G/B/A"... Octane writes ".r/.g/.b/.a".
    std::vector<std::string> channels;
    std::vector<std::pair<std::string, uint32_t>> manifest;   // (name, id), sorted by name
    std::string nameOf(uint32_t id) const;
};

struct ExrInfo {
    std::vector<ExrLayer> layers;
    std::vector<CryptoLayer> cryptos;
    int defaultLayer = 0;
};

ExrInfo ReadExrInfo(const std::wstring& path);

// What to decode from an EXR frame.
struct LoadOptions {
    // Layer: empty channels = default RGBA (fast path).
    int part = 0;
    std::vector<std::string> channels;

    // Cryptomatte: when cryptoChannels is non-empty the alpha channel receives the
    // coverage of the selected IDs (the mask); with cryptoColors RGB shows ID colors.
    int cryptoPart = 0;
    std::vector<std::string> cryptoChannels;   // CryptoLayer::channels
    std::vector<uint32_t> selection;   // sorted
    bool cryptoColors = false;

    // External Cryptomatte sequence: the matte file for each frame of the viewed
    // sequence ("" = no matte for that frame). Null = Cryptomatte of the frame itself.
    std::shared_ptr<const std::vector<std::wstring>> cryptoFiles;

    bool isDefault() const { return channels.empty() && cryptoChannels.empty(); }
    bool cryptoActive() const { return !cryptoChannels.empty(); }
    bool cryptoExternal() const { return cryptoActive() && cryptoFiles != nullptr; }
};
using LoadOptionsPtr = std::shared_ptr<const LoadOptions>;

// Layer and/or Cryptomatte of the frame itself.
ImagePtr LoadExrWithOptions(const std::wstring& path, const LoadOptions& opt);

// Several layers of one frame (Cryptomatte options ignored), read in one pass per part so
// each compressed block is decompressed once. Result is aligned with `opts`.
std::vector<ImagePtr> LoadExrLayers(const std::wstring& path, const std::vector<const LoadOptions*>& opts);

// Writes the mask of an external Cryptomatte file into a decoded frame of any format
// (result is half RGBA). A matte with a different resolution is scaled to fit.
ImagePtr ApplyExternalCrypto(const ImagePtr& frame, const std::wstring& mattePath, const LoadOptions& opt);

// Reads the Cryptomatte ranks of one pixel of `cryptoPath` (canvas coordinates of a
// canvasW x canvasH image, top-left origin) and returns the ID with the highest coverage.
bool PickCryptoId(const std::wstring& cryptoPath, const LoadOptions& opt, int x, int y, int canvasW, int canvasH,
                  uint32_t& id, float& coverage);

// Deterministic display color for an ID (used by the ID view and the UI).
void CryptoIdColor(uint32_t id, float rgb[3]);
