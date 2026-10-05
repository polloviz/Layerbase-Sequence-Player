#pragma once
#include "ExrLayers.h"
#include "Image.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

// Denoising of rendered frames with Intel Open Image Denoise (CPU or GPU; its libraries are
// downloaded on first use) or NVIDIA OptiX (part of the NVIDIA driver). Nothing is loaded
// until the first frame is denoised: the feature costs nothing at startup.

enum class DenoiseEngine : uint8_t { Oidn = 0, Optix = 1 };
enum class OidnDevice : uint8_t { Auto = 0, Cpu = 1, Gpu = 2 };
enum class OidnQuality : uint8_t { High = 0, Balanced = 1, Fast = 2 };

struct DenoiseSpec {
    DenoiseEngine engine = DenoiseEngine::Oidn;
    OidnDevice device = OidnDevice::Auto;
    OidnQuality quality = OidnQuality::High;
    // Guide layers read from the same EXR file (null = none). A normal guide needs an albedo guide.
    LoadOptionsPtr albedo, normal;
    // OptiX temporal mode: each frame also uses the denoised previous frame, moved by the motion
    // vectors (`flow`, null = still image assumed), which removes the flicker between frames.
    bool temporal = false;
    LoadOptionsPtr flow;
    bool flowInvert = false;   // the vectors point from the current frame to the previous one
    bool flowFlipY = false;    // the renderer's Y axis points up
    std::string key;   // identity of these settings, appended to LayerLoad::key
};
using DenoiseSpecPtr = std::shared_ptr<const DenoiseSpec>;

// One frame to denoise. Guides of another size are ignored.
struct DenoiseInput {
    ImagePtr color, albedo, normal, flow;
    float flowScale = 1.0f;   // motion vectors are in file pixels: 1 / proxy factor
    std::string stream;       // temporal chain: sequence, layer and resolution the frame belongs to
    int frame = -1;           // index in the sequence
};

// Denoises the RGB of the color image (alpha is kept as it is). Blocking, one frame at a time:
// called from the decode threads. On failure the result carries the error (shown as an
// unreadable frame).
ImagePtr DenoiseImage(const DenoiseInput& in, const DenoiseSpec& spec);

// Temporal mode: the decode of a frame of a stream started / ended. A frame waits for the
// previous one while that is still in progress, so with parallel decoding the chain still
// follows the frame order; otherwise a new chain starts.
void DenoiseFrameStarted(const std::string& stream, int frame);
void DenoiseFrameFinished(const std::string& stream, int frame);

// What the filter panel shows.
struct DenoiseStatus {
    std::string device;   // "NVIDIA GeForce RTX 4090 (CUDA)"
    std::string error;    // last failure, cleared by the next success
    double lastMs = 0;    // time of the last frame
    int frames = 0;       // frames denoised so far
    bool busy = false;    // a frame is being denoised now
};
DenoiseStatus GetDenoiseStatus();
// Frees the devices and their GPU memory (in the background) when the filter is switched off.
void ReleaseDenoisers();

// --- Open Image Denoise: official release from GitHub, verified, installed per user ---
extern const char* const kOidnVersion;        // "2.5.1"
constexpr int kOidnDownloadMB = 57;           // zip
constexpr int kOidnInstalledMB = 77;          // libraries kept on disk
std::wstring OidnDir();                       // %LOCALAPPDATA%\SequencePlayer\oidn-2.5.1
bool OidnInstalled();                         // file check only, nothing is loaded
bool OidnLoaded();                            // the libraries are in use (cannot be removed now)
bool RemoveOidn(std::string& err);

// Download + SHA-256 check + extraction on a worker thread; the UI polls this.
struct OidnInstall {
    std::atomic<uint64_t> received{ 0 }, total{ 0 };
    std::atomic<bool> cancel{ false }, done{ false }, ok{ false };
    std::atomic<int> stage{ 0 };   // 0 downloading, 1 verifying, 2 extracting
    std::mutex mutex;
    std::string error;             // guarded by mutex, set before done
};
// `wake` is called from the worker (a few times a second at most, and when done) to redraw the UI.
std::shared_ptr<OidnInstall> StartOidnInstall(std::function<void()> wake);

// --- OptiX: nothing to download ---
bool OptixBuiltIn();          // this build includes the OptiX denoiser
bool NvidiaDriverPresent();   // nvcuda.dll exists (file check only)

// Internal: implemented in DenoiseOptix.cpp, called with the denoise lock held. Planes are packed
// floats: RGB for color / albedo / normal, XY for flow (null = not used); `out` receives RGB.
struct OptixFrame {
    const float* rgb = nullptr;
    const float* albedo = nullptr;
    const float* normal = nullptr;
    const float* flow = nullptr;
    int width = 0, height = 0;
    bool temporal = false;
    const std::string* stream = nullptr;
    int frame = -1;
    float* out = nullptr;
};
bool OptixDenoise(const OptixFrame& f, std::string& device, std::string& err);
void OptixRelease();
