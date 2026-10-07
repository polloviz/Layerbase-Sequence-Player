#pragma once
#include "ExrLayers.h"
#include "Image.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

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
    // vector AOV (`flow`) or by motion estimated from the frames, which removes the flicker
    // between frames. Without either the image is assumed still.
    bool temporal = false;
    LoadOptionsPtr flow;
    // Motion vector AOV from another sequence: its file per frame of the sequence ("" = none for
    // that frame). Null = the AOV is a layer of the denoised file itself.
    std::shared_ptr<const std::vector<std::wstring>> flowFiles;
    bool flowInvert = false;   // the vectors point from the current frame to the previous one
    bool flowFlipY = false;    // the renderer's Y axis points up
    bool estimateFlow = false; // no AOV: motion estimated by the NVIDIA Optical Flow hardware
    // Where the previous frame does not match this one (disocclusions, wrong or missing motion),
    // the result is kept within the colors of this frame denoised on its own: no trails.
    bool antiGhost = true;
    float strength = 1.0f;     // 0 = the original frame, 1 = fully denoised
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

// Motion the temporal mode followed on a frame.
enum class DenoiseMotion : uint8_t { None = 0, Aov = 1, Estimated = 2 };

// What the filter panel shows.
struct DenoiseStatus {
    std::string device;   // "NVIDIA GeForce RTX 4090 (CUDA)"
    std::string error;    // last failure, cleared by the next success
    DenoiseMotion motion = DenoiseMotion::None;   // temporal: last chained frame
    std::string flowNote;  // why motion could not be estimated ("" = it could)
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
    bool estimateFlow = false, antiGhost = false;
    const std::string* stream = nullptr;
    int frame = -1;
    float* out = nullptr;
    // Results (temporal): the motion used, and why it could not be estimated.
    DenoiseMotion motion = DenoiseMotion::None;
    bool chained = false;
    std::string flowNote;
};
bool OptixDenoise(OptixFrame& f, std::string& device, std::string& err);
void OptixRelease();
