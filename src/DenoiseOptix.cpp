// NVIDIA OptiX denoiser. OptiX and CUDA ship with the NVIDIA driver (nvoptix.dll, nvcuda.dll):
// both are loaded on the first denoised frame and nothing is downloaded. The OptiX SDK headers
// are fetched at build time (tools/get_optix.ps1); a build without them reports the engine as
// unavailable. The temporal mode can estimate motion with NVIDIA Optical Flow (nvofapi64.dll,
// also part of the driver, loaded on the first estimate).
#include "Denoise.h"
#include "Platform.h"

#ifdef SP_HAVE_OPTIX

#include <optix.h>
#include <optix_stubs.h>
#include <optix_function_table_definition.h>
#include <nvOpticalFlowCuda.h>

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace {

// CUDA driver API entry points used here (cuda.h of the CUDA Toolkit, loaded from nvcuda.dll).
struct CudaApi {
    HMODULE lib = nullptr;
    CUresult (CUDAAPI* Init)(unsigned) = nullptr;
    CUresult (CUDAAPI* DeviceGetCount)(int*) = nullptr;
    CUresult (CUDAAPI* DeviceGet)(CUdevice*, int) = nullptr;
    CUresult (CUDAAPI* DeviceGetName)(char*, int, CUdevice) = nullptr;
    CUresult (CUDAAPI* PrimaryCtxRetain)(CUcontext*, CUdevice) = nullptr;
    CUresult (CUDAAPI* PrimaryCtxRelease)(CUdevice) = nullptr;
    CUresult (CUDAAPI* CtxPush)(CUcontext) = nullptr;
    CUresult (CUDAAPI* CtxPop)(CUcontext*) = nullptr;
    CUresult (CUDAAPI* MemAlloc)(CUdeviceptr*, size_t) = nullptr;
    CUresult (CUDAAPI* MemFree)(CUdeviceptr) = nullptr;
    CUresult (CUDAAPI* MemcpyHtoD)(CUdeviceptr, const void*, size_t) = nullptr;
    CUresult (CUDAAPI* MemcpyDtoH)(void*, CUdeviceptr, size_t) = nullptr;
    CUresult (CUDAAPI* MemcpyDtoD)(CUdeviceptr, CUdeviceptr, size_t) = nullptr;
    CUresult (CUDAAPI* Memcpy2D)(const CUDA_MEMCPY2D*) = nullptr;
    CUresult (CUDAAPI* MemsetD8)(CUdeviceptr, unsigned char, size_t) = nullptr;
    CUresult (CUDAAPI* StreamCreate)(CUstream*, unsigned) = nullptr;
    CUresult (CUDAAPI* StreamDestroy)(CUstream) = nullptr;
    CUresult (CUDAAPI* StreamSynchronize)(CUstream) = nullptr;
    CUresult (CUDAAPI* GetErrorString)(CUresult, const char**) = nullptr;
} g_cu;

// An OptiX denoiser and the GPU memory it was set up with for one frame size.
struct Model {
    OptixDenoiser denoiser = nullptr;
    CUdeviceptr state = 0, scratch = 0;
    size_t stateSize = 0, scratchSize = 0;
};

// Used with the denoise lock held (Denoise.cpp).
struct OptixState {
    bool optixLoaded = false;
    CUdevice device = 0;
    CUcontext context = nullptr;
    CUstream stream = nullptr;
    OptixDeviceContext optix = nullptr;
    std::string name;
    // `spatial` denoises single frames; it also starts the temporal chains with a clean frame.
    Model spatial, temporal;
    bool hasAlbedo = false, hasNormal = false, isTemporal = false;
    int width = 0, height = 0;   // size the models and the buffers below are set up for
    CUdeviceptr input = 0, output = 0, albedo = 0, normal = 0, flow = 0;
    size_t guidePixelSize = 0;
} g_ox;

// Temporal mode: a chain of consecutive frames of one stream (sequence + layer + resolution)
// keeps on the GPU its last denoised frame and the denoiser's internal guide layers (previous /
// current, swapped). A stream can have several chains at once: while playback wraps around,
// the end and the start of the range are decoded at the same time.
struct Chain {
    std::string stream;
    int width = 0, height = 0;
    int last = -1;                 // frame held in `output`; -1 = none
    CUdeviceptr output = 0, guide[2] = { 0, 0 };
    int current = 0;               // guide[current] holds the previous frame's internal guide
    uint64_t used = 0;
    std::vector<uint8_t> luma;     // the last frame denoised on its own (see Luma): motion and trail checks
};
std::vector<Chain> g_chains;
uint64_t g_chainClock = 0;
constexpr size_t kMaxChains = 6;   // e.g. 3 stack layers, each wrapping around

// NVIDIA Optical Flow: motion between two frames, measured by a hardware unit of Turing and later
// GPUs (GeForce RTX 20 / GTX 16 and newer). One session for the frame size in use.
struct OpticalFlow {
    HMODULE lib = nullptr;
    NV_OF_CUDA_API_FUNCTION_LIST api = {};
    NvOFHandle of = nullptr;
    NvOFGPUBufferHandle input = nullptr, reference = nullptr, output = nullptr;
    CUdeviceptr inputPtr = 0, referencePtr = 0, outputPtr = 0;
    uint32_t inputPitch = 0, referencePitch = 0, outputPitch = 0;
    int width = 0, height = 0, grid = 0;   // size of the session; output: one vector per grid x grid pixels
    std::string unavailable;               // not on this computer (no unit, driver too old): not tried again
    int badWidth = 0, badHeight = 0;       // a frame size outside the unit's limits...
    std::string badSize;                   // ...and the message for it
} g_of;

// Host planes of the temporal mode, kept between frames: allocating them for every frame costs
// more than the work done on them. Freed with the denoisers.
struct Planes {
    std::vector<float> spatial;   // this frame denoised on its own (RGB)
    std::vector<float> flow;      // estimated motion (XY)
    std::vector<float> trust;
    std::vector<float> a, b, tmp; // working planes
    std::vector<NV_OF_FLOW_VECTOR> vectors;
} g_planes;

template <class F>
bool Resolve(F& fn, const char* name)
{
    fn = reinterpret_cast<F>(GetProcAddress(g_cu.lib, name));
    return fn != nullptr;
}

bool LoadCuda(std::string& err)
{
    if (g_cu.lib) return true;
    HMODULE lib = LoadLibraryExW(L"nvcuda.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!lib) { err = "NVIDIA driver not found (nvcuda.dll)"; return false; }
    g_cu.lib = lib;
    const bool ok = Resolve(g_cu.Init, "cuInit") && Resolve(g_cu.DeviceGetCount, "cuDeviceGetCount") &&
                    Resolve(g_cu.DeviceGet, "cuDeviceGet") && Resolve(g_cu.DeviceGetName, "cuDeviceGetName") &&
                    Resolve(g_cu.PrimaryCtxRetain, "cuDevicePrimaryCtxRetain") &&
                    Resolve(g_cu.PrimaryCtxRelease, "cuDevicePrimaryCtxRelease_v2") &&
                    Resolve(g_cu.CtxPush, "cuCtxPushCurrent_v2") && Resolve(g_cu.CtxPop, "cuCtxPopCurrent_v2") &&
                    Resolve(g_cu.MemAlloc, "cuMemAlloc_v2") && Resolve(g_cu.MemFree, "cuMemFree_v2") &&
                    Resolve(g_cu.MemcpyHtoD, "cuMemcpyHtoD_v2") && Resolve(g_cu.MemcpyDtoH, "cuMemcpyDtoH_v2") &&
                    Resolve(g_cu.MemcpyDtoD, "cuMemcpyDtoD_v2") && Resolve(g_cu.Memcpy2D, "cuMemcpy2D_v2") &&
                    Resolve(g_cu.MemsetD8, "cuMemsetD8_v2") &&
                    Resolve(g_cu.StreamCreate, "cuStreamCreate") && Resolve(g_cu.StreamDestroy, "cuStreamDestroy_v2") &&
                    Resolve(g_cu.StreamSynchronize, "cuStreamSynchronize") && Resolve(g_cu.GetErrorString, "cuGetErrorString");
    if (!ok) {
        g_cu = CudaApi();
        err = "NVIDIA driver too old (nvcuda.dll)";
        return false;
    }
    return true;
}

bool CudaOk(CUresult r, const char* what, std::string& err)
{
    if (r == 0) return true;
    const char* msg = nullptr;
    if (g_cu.GetErrorString) g_cu.GetErrorString(r, &msg);
    err = std::string(what) + ": " + (msg ? msg : "CUDA error " + std::to_string(r));
    return false;
}

bool OptixOk(OptixResult r, const char* what, std::string& err)
{
    if (r == OPTIX_SUCCESS) return true;
    err = std::string(what) + ": " + optixGetErrorName(r);
    return false;
}

// Makes the CUDA context current on this decode thread for the duration of a call.
struct ContextScope {
    bool pushed = false;
    ContextScope() { pushed = g_ox.context && g_cu.CtxPush(g_ox.context) == 0; }
    ~ContextScope() {
        CUcontext c = nullptr;
        if (pushed) g_cu.CtxPop(&c);
    }
};

void Free(CUdeviceptr& p)
{
    if (p) g_cu.MemFree(p);
    p = 0;
}

void FreeModelMemory(Model& m)
{
    Free(m.state);
    Free(m.scratch);
    m.stateSize = m.scratchSize = 0;
}

void FreeBuffers()
{
    FreeModelMemory(g_ox.spatial);
    FreeModelMemory(g_ox.temporal);
    for (CUdeviceptr* p : { &g_ox.input, &g_ox.output, &g_ox.albedo, &g_ox.normal, &g_ox.flow }) Free(*p);
    g_ox.width = g_ox.height = 0;
}

void FreeChain(Chain& c)
{
    for (CUdeviceptr* p : { &c.output, &c.guide[0], &c.guide[1] }) Free(*p);
    c = Chain();
}

void FreeChains()
{
    for (Chain& c : g_chains) FreeChain(c);
    g_chains.clear();
}

void OfClose()
{
    if (g_of.of) {
        for (NvOFGPUBufferHandle* b : { &g_of.input, &g_of.reference, &g_of.output })
            if (*b) { g_of.api.nvOFDestroyGPUBufferCuda(*b); *b = nullptr; }
        g_of.api.nvOFDestroy(g_of.of);
        g_of.of = nullptr;
    }
    g_of.inputPtr = g_of.referencePtr = g_of.outputPtr = 0;
    g_of.width = g_of.height = g_of.grid = 0;
}

void DestroyDenoisers()
{
    FreeBuffers();
    FreeChains();
    OfClose();
    g_planes = Planes();
    for (Model* m : { &g_ox.spatial, &g_ox.temporal })
        if (m->denoiser) { optixDenoiserDestroy(m->denoiser); m->denoiser = nullptr; }
}

bool Init(std::string& err)
{
    if (g_ox.optix) return true;
    if (!LoadCuda(err)) return false;
    int count = 0;
    if (!CudaOk(g_cu.Init(0), "cuInit", err) || !CudaOk(g_cu.DeviceGetCount(&count), "cuDeviceGetCount", err)) return false;
    if (count < 1) { err = "no NVIDIA GPU found"; return false; }
    if (!CudaOk(g_cu.DeviceGet(&g_ox.device, 0), "cuDeviceGet", err)) return false;
    char name[256] = {};
    g_cu.DeviceGetName(name, sizeof(name) - 1, g_ox.device);
    if (!CudaOk(g_cu.PrimaryCtxRetain(&g_ox.context, g_ox.device), "cuDevicePrimaryCtxRetain", err)) return false;
    bool ok = true;
    {
        ContextScope scope;
        if (!scope.pushed) { ok = false; err = "cannot use the CUDA context"; }
        if (ok && !g_ox.optixLoaded) {
            const OptixResult r = optixInit();
            if (r == OPTIX_ERROR_LIBRARY_NOT_FOUND) { ok = false; err = "OptiX not found in the NVIDIA driver (nvoptix.dll): update the driver"; }
            else if (r == OPTIX_ERROR_UNSUPPORTED_ABI_VERSION) { ok = false; err = "the NVIDIA driver is too old for OptiX 8 (R535 or later needed)"; }
            else ok = OptixOk(r, "optixInit", err);
            g_ox.optixLoaded = ok;
        }
        OptixDeviceContextOptions options = {};
        ok = ok && OptixOk(optixDeviceContextCreate(g_ox.context, &options, &g_ox.optix), "optixDeviceContextCreate", err) &&
             CudaOk(g_cu.StreamCreate(&g_ox.stream, 0), "cuStreamCreate", err);
        if (!ok && g_ox.optix) { optixDeviceContextDestroy(g_ox.optix); g_ox.optix = nullptr; }
    }
    if (!ok) {
        g_cu.PrimaryCtxRelease(g_ox.device);
        g_ox.context = nullptr;
        return false;
    }
    g_ox.name = std::string(name) + " (OptiX)";
    Log("denoise: OptiX on %s", name);
    return true;
}

OptixImage2D Plane(CUdeviceptr data, int w, int h, OptixPixelFormat format = OPTIX_PIXEL_FORMAT_FLOAT3, unsigned pixelSize = 12)
{
    OptixImage2D img = {};
    img.data = data;
    img.width = unsigned(w);
    img.height = unsigned(h);
    img.pixelStrideInBytes = pixelSize;
    img.rowStrideInBytes = unsigned(w) * pixelSize;
    img.format = format;
    return img;
}

OptixImage2D GuidePlane(CUdeviceptr data, int w, int h)
{
    return Plane(data, w, h, OPTIX_PIXEL_FORMAT_INTERNAL_GUIDE_LAYER, unsigned(g_ox.guidePixelSize));
}

bool CreateModel(Model& m, OptixDenoiserModelKind kind, bool albedo, bool normal, std::string& err)
{
    // The AOV models are the current ones; with null intensity / average color they are computed per frame.
    OptixDenoiserOptions options = {};
    options.guideAlbedo = albedo ? 1 : 0;
    options.guideNormal = normal ? 1 : 0;
    options.denoiseAlpha = OPTIX_DENOISER_ALPHA_MODE_COPY;
    return OptixOk(optixDenoiserCreate(g_ox.optix, kind, &options, &m.denoiser), "optixDenoiserCreate", err);
}

bool SetupModel(Model& m, int w, int h, std::string& err)
{
    OptixDenoiserSizes sizes = {};
    if (!OptixOk(optixDenoiserComputeMemoryResources(m.denoiser, unsigned(w), unsigned(h), &sizes), "optixDenoiserComputeMemoryResources", err))
        return false;
    m.stateSize = sizes.stateSizeInBytes;
    m.scratchSize = sizes.withoutOverlapScratchSizeInBytes;
    if (sizes.internalGuideLayerPixelSizeInBytes) {
        if (g_ox.guidePixelSize != sizes.internalGuideLayerPixelSizeInBytes) FreeChains();
        g_ox.guidePixelSize = sizes.internalGuideLayerPixelSizeInBytes;
    }
    return CudaOk(g_cu.MemAlloc(&m.state, m.stateSize), "cuMemAlloc", err) &&
           CudaOk(g_cu.MemAlloc(&m.scratch, m.scratchSize), "cuMemAlloc", err) &&
           OptixOk(optixDenoiserSetup(m.denoiser, g_ox.stream, unsigned(w), unsigned(h), m.state, m.stateSize, m.scratch, m.scratchSize),
                   "optixDenoiserSetup", err);
}

bool Invoke(Model& m, const OptixDenoiserParams& params, const OptixDenoiserGuideLayer& guide, const OptixDenoiserLayer& layer,
            std::string& err)
{
    return OptixOk(optixDenoiserInvoke(m.denoiser, g_ox.stream, &params, m.state, m.stateSize, &guide, &layer, 1, 0, 0, m.scratch,
                                       m.scratchSize), "optixDenoiserInvoke", err);
}

// ---------------------------------------------------------------------------
// Temporal mode: motion estimation and the trail check, on packed host pixels.

// Runs fn(y0, y1) on bands of rows, on every core: a few ms per frame of per-pixel work. The
// bands go to the Windows thread pool, whose threads stay around between calls.
template <class F>
void ParallelRows(int h, const F& fn)
{
    struct Work {
        const F& fn;
        int h, band, bands;
        std::atomic<int> next{ 0 }, helping{ 0 };
        std::mutex m;
        std::condition_variable cv;
        void run()
        {
            for (int b; (b = next++) < bands;) fn(b * band, std::min(h, (b + 1) * band));
        }
    };
    const int bands = std::clamp(int(std::thread::hardware_concurrency()), 1, 32);
    Work work{ fn, h, (h + bands - 1) / bands, bands };
    for (int i = 1; i < bands; ++i) {
        ++work.helping;
        auto help = [](PTP_CALLBACK_INSTANCE, void* p) {
            Work& w = *static_cast<Work*>(p);
            w.run();
            std::lock_guard l(w.m);
            if (--w.helping == 0) w.cv.notify_one();
        };
        if (!TrySubmitThreadpoolCallback(help, &work, nullptr)) --work.helping;
    }
    work.run();
    std::unique_lock l(work.m);
    work.cv.wait(l, [&] { return work.helping == 0; });
}

bool Copy2D(CUmemorytype dstType, void* dstHost, CUdeviceptr dstDevice, size_t dstPitch, CUmemorytype srcType, const void* srcHost,
            CUdeviceptr srcDevice, size_t srcPitch, size_t rowBytes, size_t rows, std::string& err)
{
    CUDA_MEMCPY2D c = {};
    c.srcMemoryType = srcType;
    c.srcHost = srcHost;
    c.srcDevice = srcDevice;
    c.srcPitch = srcPitch;
    c.dstMemoryType = dstType;
    c.dstHost = dstHost;
    c.dstDevice = dstDevice;
    c.dstPitch = dstPitch;
    c.WidthInBytes = rowBytes;
    c.Height = rows;
    return CudaOk(g_cu.Memcpy2D(&c), "cuMemcpy2D", err);
}

// 8-bit luma of a linear RGB frame, as the Optical Flow unit reads it: highlights compressed
// (x / (1 + x)) and a square-root curve, so shadows keep their detail.
std::vector<uint8_t> Luma(const std::vector<float>& rgb, int w, int h)
{
    std::vector<uint8_t> out(size_t(w) * h);
    ParallelRows(h, [&](int y0, int y1) {
        for (size_t i = size_t(y0) * w; i < size_t(y1) * w; ++i) {
            const float y = std::max(0.0f, 0.2126f * rgb[i * 3] + 0.7152f * rgb[i * 3 + 1] + 0.0722f * rgb[i * 3 + 2]);
            out[i] = uint8_t(std::sqrt(y / (1.0f + y)) * 255.0f + 0.5f);
        }
    });
    return out;
}

// The previous frame's luma at (px, py) of this frame (bilinear); -1 outside the frame.
inline float Sample(const std::vector<uint8_t>& prev, int w, int h, float px, float py)
{
    if (!(px >= 0 && py >= 0 && px <= w - 1 && py <= h - 1)) return -1;
    const int ax = int(px), ay = int(py), bx = std::min(ax + 1, w - 1), by = std::min(ay + 1, h - 1);
    const float tx = px - ax, ty = py - ay;
    const uint8_t* r0 = &prev[size_t(ay) * w];
    const uint8_t* r1 = &prev[size_t(by) * w];
    return (r0[ax] * (1 - tx) + r0[bx] * tx) * (1 - ty) + (r1[ax] * (1 - tx) + r1[bx] * tx) * ty;
}

// Mean over the (2r + 1) x (2r + 1) pixels around each one (edges repeated), with running
// sums, in place; `across` is a working plane.
void BoxMean(std::vector<float>& data, std::vector<float>& across, int w, int h, int r)
{
    across.resize(data.size());
    ParallelRows(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const float* row = &data[size_t(y) * w];
            float* dst = &across[size_t(y) * w];
            float sum = 0;
            for (int k = -r; k <= r; ++k) sum += row[std::clamp(k, 0, w - 1)];
            for (int x = 0; x < w; ++x) {
                dst[x] = sum;
                sum += row[std::min(x + r + 1, w - 1)] - row[std::max(x - r, 0)];
            }
        }
    });
    const float norm = 1.0f / float((2 * r + 1) * (2 * r + 1));
    ParallelRows(h, [&](int y0, int y1) {
        if (y0 >= y1) return;
        std::vector<float> sum(w, 0.0f);
        for (int k = -r; k <= r; ++k) {
            const float* row = &across[size_t(std::clamp(y0 + k, 0, h - 1)) * w];
            for (int x = 0; x < w; ++x) sum[x] += row[x];
        }
        for (int y = y0; y < y1; ++y) {
            float* dst = &data[size_t(y) * w];
            const float* add = &across[size_t(std::min(y + r + 1, h - 1)) * w];
            const float* drop = &across[size_t(std::max(y - r, 0)) * w];
            for (int x = 0; x < w; ++x) {
                dst[x] = sum[x] * norm;
                sum[x] += add[x] - drop[x];
            }
        }
    });
}

bool OfOk(NV_OF_STATUS s, const char* what, std::string& err)
{
    if (s == NV_OF_SUCCESS) return true;
    err = std::string(what) + " failed (" + std::to_string(int(s)) + ")";
    char msg[256] = {};
    uint32_t size = sizeof(msg);
    if (g_of.of && g_of.api.nvOFGetLastError && g_of.api.nvOFGetLastError(g_of.of, msg, &size) == NV_OF_SUCCESS && msg[0])
        err += ": " + std::string(msg, strnlen(msg, sizeof(msg)));
    return false;
}

// An Optical Flow session for frames of w x h. False with the reason when there is none.
bool OfOpen(int w, int h, std::string& err)
{
    if (g_of.of && g_of.width == w && g_of.height == h) return true;
    if (!g_of.unavailable.empty()) { err = g_of.unavailable; return false; }
    if (g_of.badWidth == w && g_of.badHeight == h) { err = g_of.badSize; return false; }
    OfClose();
    if (!g_of.lib) {
        HMODULE lib = LoadLibraryExW(L"nvofapi64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        using CreateInstance = NV_OF_STATUS(NVOFAPI*)(uint32_t, NV_OF_CUDA_API_FUNCTION_LIST*);
        auto create = lib ? reinterpret_cast<CreateInstance>(GetProcAddress(lib, "NvOFAPICreateInstanceCuda")) : nullptr;
        if (!create || create(NV_OF_API_VERSION, &g_of.api) != NV_OF_SUCCESS) {
            g_of.unavailable = !lib ? "NVIDIA Optical Flow not found in the driver (nvofapi64.dll)"
                                    : "the NVIDIA driver is too old for Optical Flow";
            if (lib) FreeLibrary(lib);
            err = g_of.unavailable;
            return false;
        }
        g_of.lib = lib;
        Log("denoise: NVIDIA Optical Flow loaded");
    }
    const NV_OF_STATUS created = g_of.api.nvCreateOpticalFlowCuda(g_ox.context, &g_of.of);
    if (created != NV_OF_SUCCESS) {
        g_of.of = nullptr;
        g_of.unavailable = created == NV_OF_ERR_OF_NOT_AVAILABLE || created == NV_OF_ERR_UNSUPPORTED_DEVICE
                               ? "this GPU has no Optical Flow unit (GeForce RTX 20 / GTX 16 or later)"
                               : "cannot start NVIDIA Optical Flow (" + std::to_string(int(created)) + ")";
        err = g_of.unavailable;
        return false;
    }
    auto caps = [](NV_OF_CAPS cap, std::vector<uint32_t>& v) {
        uint32_t size = 0;
        if (g_of.api.nvOFGetCaps(g_of.of, cap, nullptr, &size) != NV_OF_SUCCESS || size == 0) return false;
        v.assign(size, 0);
        return g_of.api.nvOFGetCaps(g_of.of, cap, v.data(), &size) == NV_OF_SUCCESS;
    };
    std::vector<uint32_t> v;
    uint32_t minW = 1, minH = 1, maxW = UINT32_MAX, maxH = UINT32_MAX;
    if (caps(NV_OF_CAPS_WIDTH_MIN, v)) minW = v[0];
    if (caps(NV_OF_CAPS_HEIGHT_MIN, v)) minH = v[0];
    if (caps(NV_OF_CAPS_WIDTH_MAX, v)) maxW = v[0];
    if (caps(NV_OF_CAPS_HEIGHT_MAX, v)) maxH = v[0];
    if (uint32_t(w) < minW || uint32_t(h) < minH || uint32_t(w) > maxW || uint32_t(h) > maxH) {
        g_of.badWidth = w;
        g_of.badHeight = h;
        g_of.badSize = "frame size outside the Optical Flow limits (" + std::to_string(minW) + "x" + std::to_string(minH) + " to " +
                       std::to_string(maxW) + "x" + std::to_string(maxH) + ")";
        err = g_of.badSize;
        OfClose();
        return false;
    }
    // The finest output grid the unit has (1 x 1 from Ampere on, 4 x 4 on Turing).
    int grid = 0;
    if (caps(NV_OF_CAPS_SUPPORTED_OUTPUT_GRID_SIZES, v))
        for (uint32_t g : v)
            if (g >= 1 && g <= 4 && (!grid || int(g) < grid)) grid = int(g);
    if (!grid) grid = 4;

    NV_OF_INIT_PARAMS init = {};
    init.width = uint32_t(w);
    init.height = uint32_t(h);
    init.outGridSize = NV_OF_OUTPUT_VECTOR_GRID_SIZE(grid);
    init.hintGridSize = NV_OF_HINT_VECTOR_GRID_SIZE_UNDEFINED;
    init.mode = NV_OF_MODE_OPTICALFLOW;
    init.perfLevel = NV_OF_PERF_LEVEL_SLOW;   // best quality: the unit is fast anyway
    auto buffer = [&](NV_OF_BUFFER_USAGE usage, NV_OF_BUFFER_FORMAT format, int bw, int bh, NvOFGPUBufferHandle& b, CUdeviceptr& ptr,
                      uint32_t& pitch) {
        NV_OF_BUFFER_DESCRIPTOR d = {};
        d.width = uint32_t(bw);
        d.height = uint32_t(bh);
        d.bufferUsage = usage;
        d.bufferFormat = format;
        if (!OfOk(g_of.api.nvOFCreateGPUBufferCuda(g_of.of, &d, NV_OF_CUDA_BUFFER_TYPE_CUDEVICEPTR, &b), "nvOFCreateGPUBufferCuda", err))
            return false;
        NV_OF_CUDA_BUFFER_STRIDE_INFO stride = {};
        if (!OfOk(g_of.api.nvOFGPUBufferGetStrideInfo(b, &stride), "nvOFGPUBufferGetStrideInfo", err)) return false;
        ptr = g_of.api.nvOFGPUBufferGetCUdeviceptr(b);
        pitch = stride.strideInfo[0].strideXInBytes;
        if (!ptr) err = "Optical Flow buffer without memory";
        return ptr != 0;
    };
    const bool ok = OfOk(g_of.api.nvOFInit(g_of.of, &init), "nvOFInit", err) &&
                    OfOk(g_of.api.nvOFSetIOCudaStreams(g_of.of, g_ox.stream, g_ox.stream), "nvOFSetIOCudaStreams", err) &&
                    buffer(NV_OF_BUFFER_USAGE_INPUT, NV_OF_BUFFER_FORMAT_GRAYSCALE8, w, h, g_of.input, g_of.inputPtr, g_of.inputPitch) &&
                    buffer(NV_OF_BUFFER_USAGE_INPUT, NV_OF_BUFFER_FORMAT_GRAYSCALE8, w, h, g_of.reference, g_of.referencePtr, g_of.referencePitch) &&
                    buffer(NV_OF_BUFFER_USAGE_OUTPUT, NV_OF_BUFFER_FORMAT_SHORT2, (w + grid - 1) / grid, (h + grid - 1) / grid, g_of.output,
                           g_of.outputPtr, g_of.outputPitch);
    if (!ok) {
        OfClose();
        return false;
    }
    g_of.width = w;
    g_of.height = h;
    g_of.grid = grid;
    Log("denoise: Optical Flow session %dx%d, grid %d", w, h, grid);
    return true;
}

// Motion from the previous frame to this one (their lumas), as packed XY per pixel in the OptiX
// convention: pixel movement from the previous frame to the current one.
bool EstimateFlow(const std::vector<uint8_t>& cur, const std::vector<uint8_t>& prev, int w, int h, std::vector<float>& flow,
                  std::string& err)
{
    if (!OfOpen(w, h, err)) return false;
    if (!Copy2D(CU_MEMORYTYPE_DEVICE, nullptr, g_of.inputPtr, g_of.inputPitch, CU_MEMORYTYPE_HOST, cur.data(), 0, size_t(w), size_t(w),
                size_t(h), err) ||
        !Copy2D(CU_MEMORYTYPE_DEVICE, nullptr, g_of.referencePtr, g_of.referencePitch, CU_MEMORYTYPE_HOST, prev.data(), 0, size_t(w),
                size_t(w), size_t(h), err))
        return false;
    NV_OF_EXECUTE_INPUT_PARAMS in = {};
    in.inputFrame = g_of.input;
    in.referenceFrame = g_of.reference;
    in.disableTemporalHints = NV_OF_TRUE;   // the session serves several chains in turn
    NV_OF_EXECUTE_OUTPUT_PARAMS out = {};
    out.outputBuffer = g_of.output;
    if (!OfOk(g_of.api.nvOFExecute(g_of.of, &in, &out), "nvOFExecute", err) ||
        !CudaOk(g_cu.StreamSynchronize(g_ox.stream), "cuStreamSynchronize", err))
        return false;
    const int g = g_of.grid, gw = (w + g - 1) / g, gh = (h + g - 1) / g;
    const size_t rowBytes = size_t(gw) * sizeof(NV_OF_FLOW_VECTOR);
    std::vector<NV_OF_FLOW_VECTOR>& v = g_planes.vectors;
    v.resize(size_t(gw) * gh);
    if (!Copy2D(CU_MEMORYTYPE_HOST, v.data(), 0, rowBytes, CU_MEMORYTYPE_DEVICE, nullptr, g_of.outputPtr, g_of.outputPitch, rowBytes,
                size_t(gh), err))
        return false;
    // Per block of the current frame, the unit gives where it was in the previous one (S10.5
    // fixed point): negated, and interpolated between block centers.
    flow.resize(size_t(w) * h * 2);
    ParallelRows(h, [&](int y0, int y1) {
        if (g == 1) {
            for (size_t i = size_t(y0) * w; i < size_t(y1) * w; ++i) {
                flow[i * 2] = v[i].flowx * (-1.0f / 32.0f);
                flow[i * 2 + 1] = v[i].flowy * (-1.0f / 32.0f);
            }
            return;
        }
        for (int y = y0; y < y1; ++y) {
            const float fy = std::clamp((y + 0.5f) / g - 0.5f, 0.0f, float(gh - 1));
            const int by = int(fy), by1 = std::min(by + 1, gh - 1);
            const float ty = fy - by;
            for (int x = 0; x < w; ++x) {
                const float fx = std::clamp((x + 0.5f) / g - 0.5f, 0.0f, float(gw - 1));
                const int bx = int(fx), bx1 = std::min(bx + 1, gw - 1);
                const float tx = fx - bx;
                const NV_OF_FLOW_VECTOR &a = v[size_t(by) * gw + bx], &b = v[size_t(by) * gw + bx1];
                const NV_OF_FLOW_VECTOR &c = v[size_t(by1) * gw + bx], &d = v[size_t(by1) * gw + bx1];
                const float mx = (a.flowx * (1 - tx) + b.flowx * tx) * (1 - ty) + (c.flowx * (1 - tx) + d.flowx * tx) * ty;
                const float my = (a.flowy * (1 - tx) + b.flowy * tx) * (1 - ty) + (c.flowy * (1 - tx) + d.flowy * tx) * ty;
                flow[(size_t(y) * w + x) * 2] = -mx / 32.0f;
                flow[(size_t(y) * w + x) * 2 + 1] = -my / 32.0f;
            }
        }
    });
    // Flat areas (a backdrop, a sky) give the unit nothing to match: its vectors there are noise,
    // which would drag the previous frame around. A vector is kept where it clearly explains the
    // change around the pixel better than no motion does (beyond what the leftover noise does).
    std::vector<float>& still = g_planes.a;
    std::vector<float>& followed = g_planes.b;
    still.resize(cur.size());
    followed.resize(cur.size());
    ParallelRows(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
            for (int x = 0; x < w; ++x) {
                const size_t i = size_t(y) * w + x;
                const float moved = Sample(prev, w, h, x - flow[i * 2], y - flow[i * 2 + 1]);
                still[i] = std::abs(float(cur[i]) - float(prev[i]));
                followed[i] = moved < 0 ? 255.0f : std::abs(float(cur[i]) - moved);
            }
    });
    BoxMean(still, g_planes.tmp, w, h, 3);
    BoxMean(followed, g_planes.tmp, w, h, 3);
    ParallelRows(h, [&](int y0, int y1) {
        for (size_t i = size_t(y0) * w; i < size_t(y1) * w; ++i) {
            const float keep = std::clamp((still[i] - 1.5f * followed[i] - 3.0f) / 2.0f, 0.0f, 1.0f);   // luma steps
            flow[i * 2] *= keep;
            flow[i * 2 + 1] *= keep;
        }
    });
    return true;
}

// How well the previous frame, moved by `flow` (null = still), matches this one, per pixel:
// 1 = the same, 0 = something else is there (disocclusion, motion missing or wrong). Lumas of
// both frames denoised on their own. The difference is averaged over 5 x 5 pixels, so what the
// denoiser left of the noise cancels out and only a real change counts.
void Trust(const std::vector<uint8_t>& cur, const std::vector<uint8_t>& prev, const float* flow, int w, int h, std::vector<float>& out)
{
    constexpr float kSame = 2.5f, kDifferent = 10.0f;   // mean luma difference, in 8-bit steps
    std::vector<float>& diff = g_planes.a;
    diff.resize(cur.size());
    ParallelRows(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
            for (int x = 0; x < w; ++x) {
                const size_t i = size_t(y) * w + x;
                const float moved = flow ? Sample(prev, w, h, x - flow[i * 2], y - flow[i * 2 + 1]) : float(prev[i]);
                diff[i] = moved < 0 ? 255.0f : float(cur[i]) - moved;   // from outside the frame: no match
            }
    });
    BoxMean(diff, g_planes.tmp, w, h, 2);
    ParallelRows(h, [&](int y0, int y1) {
        for (size_t i = size_t(y0) * w; i < size_t(y1) * w; ++i)
            diff[i] = std::clamp((kDifferent - std::abs(diff[i])) / (kDifferent - kSame), 0.0f, 1.0f);
    });
    // 3 x 3 minimum: trails sit right next to the edges that moved.
    std::vector<float>& across = g_planes.tmp;
    across.resize(diff.size());
    out.resize(diff.size());
    ParallelRows(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const float* row = &diff[size_t(y) * w];
            float* dst = &across[size_t(y) * w];
            for (int x = 0; x < w; ++x) dst[x] = std::min({ row[std::max(x - 1, 0)], row[x], row[std::min(x + 1, w - 1)] });
        }
    });
    ParallelRows(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const float* up = &across[size_t(std::max(y - 1, 0)) * w];
            const float* mid = &across[size_t(y) * w];
            const float* down = &across[size_t(std::min(y + 1, h - 1)) * w];
            float* dst = &out[size_t(y) * w];
            for (int x = 0; x < w; ++x) dst[x] = std::min({ up[x], mid[x], down[x] });
        }
    });
}

// Where the previous frame did not match (low trust), the temporal result is kept within the
// colors around the pixel in this frame denoised on its own (3 x 3 minimum / maximum, as
// temporal anti-aliasing does): what is not in this frame cannot trail. Elsewhere it is left
// as it is, so still areas keep their temporal stability.
void Resolve(float* out, const std::vector<float>& spatial, const std::vector<float>& trust, int w, int h)
{
    ParallelRows(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
            for (int x = 0; x < w; ++x) {
                const size_t i = size_t(y) * w + x;
                const float t = trust[i];
                if (t >= 1.0f) continue;
                for (int c = 0; c < 3; ++c) {
                    float lo = spatial[i * 3 + c], hi = lo;
                    for (int dy = -1; dy <= 1; ++dy) {
                        const size_t row = size_t(std::clamp(y + dy, 0, h - 1)) * w;
                        for (int dx = -1; dx <= 1; ++dx) {
                            const float v = spatial[(row + std::clamp(x + dx, 0, w - 1)) * 3 + c];
                            lo = std::min(lo, v);
                            hi = std::max(hi, v);
                        }
                    }
                    float& o = out[i * 3 + c];
                    o = o * t + std::clamp(o, lo, hi) * (1.0f - t);
                }
            }
    });
}


// The chain that `frame` continues (the one that ended on the frame before), else a new one:
// a free slot, or the least recently used chain.
Chain* UseChain(const std::string& stream, int frame, int w, int h, bool& chained, std::string& err)
{
    for (Chain& c : g_chains)
        if (c.stream == stream && c.width == w && c.height == h && c.last >= 0 && c.last == frame - 1) {
            chained = true;
            c.used = ++g_chainClock;
            return &c;
        }
    chained = false;
    Chain* c = nullptr;
    if (g_chains.size() < kMaxChains) c = &g_chains.emplace_back();
    else c = &*std::min_element(g_chains.begin(), g_chains.end(), [](const Chain& a, const Chain& b) { return a.used < b.used; });
    if (c->width != w || c->height != h) {
        FreeChain(*c);
        const size_t bytes = size_t(w) * h * 3 * sizeof(float), guide = size_t(w) * h * g_ox.guidePixelSize;
        if (!CudaOk(g_cu.MemAlloc(&c->output, bytes), "cuMemAlloc", err) || !CudaOk(g_cu.MemAlloc(&c->guide[0], guide), "cuMemAlloc", err) ||
            !CudaOk(g_cu.MemAlloc(&c->guide[1], guide), "cuMemAlloc", err)) {
            FreeChain(*c);
            return nullptr;
        }
        c->width = w;
        c->height = h;
    }
    c->stream = stream;
    c->last = -1;
    c->current = 0;
    c->used = ++g_chainClock;
    return c;
}

bool Run(OptixFrame& f, std::string& err)
{
    const int w = f.width, h = f.height;
    const bool albedo = f.albedo != nullptr, normal = f.normal != nullptr;
    ContextScope scope;
    if (!scope.pushed) { err = "cannot use the CUDA context"; return false; }
    if (!g_ox.spatial.denoiser || g_ox.hasAlbedo != albedo || g_ox.hasNormal != normal || g_ox.isTemporal != f.temporal) {
        DestroyDenoisers();
        if (!CreateModel(g_ox.spatial, OPTIX_DENOISER_MODEL_KIND_AOV, albedo, normal, err) ||
            (f.temporal && !CreateModel(g_ox.temporal, OPTIX_DENOISER_MODEL_KIND_TEMPORAL_AOV, albedo, normal, err)))
            return false;
        g_ox.hasAlbedo = albedo;
        g_ox.hasNormal = normal;
        g_ox.isTemporal = f.temporal;
    }
    const size_t bytes = size_t(w) * h * 3 * sizeof(float), flowBytes = size_t(w) * h * 2 * sizeof(float);
    if (g_ox.width != w || g_ox.height != h) {
        FreeBuffers();
        const bool ok = SetupModel(g_ox.spatial, w, h, err) && (!f.temporal || SetupModel(g_ox.temporal, w, h, err)) &&
                        CudaOk(g_cu.MemAlloc(&g_ox.input, bytes), "cuMemAlloc", err) &&
                        CudaOk(g_cu.MemAlloc(&g_ox.output, bytes), "cuMemAlloc", err) &&
                        (!f.temporal || CudaOk(g_cu.MemAlloc(&g_ox.flow, flowBytes), "cuMemAlloc", err)) &&
                        (!albedo || CudaOk(g_cu.MemAlloc(&g_ox.albedo, bytes), "cuMemAlloc", err)) &&
                        (!normal || CudaOk(g_cu.MemAlloc(&g_ox.normal, bytes), "cuMemAlloc", err));
        if (!ok) { FreeBuffers(); return false; }
        g_ox.width = w;
        g_ox.height = h;
    }
    if (!CudaOk(g_cu.MemcpyHtoD(g_ox.input, f.rgb, bytes), "cuMemcpyHtoD", err) ||
        (albedo && !CudaOk(g_cu.MemcpyHtoD(g_ox.albedo, f.albedo, bytes), "cuMemcpyHtoD", err)) ||
        (normal && !CudaOk(g_cu.MemcpyHtoD(g_ox.normal, f.normal, bytes), "cuMemcpyHtoD", err)))
        return false;

    OptixDenoiserParams params = {};
    OptixDenoiserGuideLayer guide = {};
    if (albedo) guide.albedo = Plane(g_ox.albedo, w, h);
    if (normal) guide.normal = Plane(g_ox.normal, w, h);
    OptixDenoiserLayer layer = {};
    layer.input = Plane(g_ox.input, w, h);

    if (!f.temporal) {
        layer.output = Plane(g_ox.output, w, h);
        return Invoke(g_ox.spatial, params, guide, layer, err) && CudaOk(g_cu.StreamSynchronize(g_ox.stream), "cuStreamSynchronize", err) &&
               CudaOk(g_cu.MemcpyDtoH(f.out, g_ox.output, bytes), "cuMemcpyDtoH", err);
    }

    bool chained = false;
    Chain* c = UseChain(*f.stream, f.frame, w, h, chained, err);
    if (!c) return false;
    f.chained = chained;
    c->last = -1;   // until this frame succeeds

    // This frame denoised on its own: the start of a chain, and what motion is estimated and
    // trails are found with.
    const bool compare = f.antiGhost || f.estimateFlow;
    std::vector<float>& spatial = g_planes.spatial;
    std::vector<uint8_t> luma;
    if (!chained || compare) {
        // A chain starts: its "previous" frame is this one denoised on its own (cleaner than the
        // noisy frame the documentation allows), with zero motion and cleared guides.
        if (!chained) Log("denoise: temporal chain starts at frame %d", f.frame);
        layer.output = Plane(compare ? g_ox.output : c->output, w, h);
        if (!Invoke(g_ox.spatial, params, guide, layer, err)) return false;
        if (!chained && (!CudaOk(g_cu.MemsetD8(c->guide[c->current], 0, size_t(w) * h * g_ox.guidePixelSize), "cuMemsetD8", err) ||
                         (compare && !CudaOk(g_cu.MemcpyDtoD(c->output, g_ox.output, bytes), "cuMemcpyDtoD", err))))
            return false;
        if (compare) {
            spatial.resize(size_t(w) * h * 3);
            if (!CudaOk(g_cu.StreamSynchronize(g_ox.stream), "cuStreamSynchronize", err) ||
                !CudaOk(g_cu.MemcpyDtoH(spatial.data(), g_ox.output, bytes), "cuMemcpyDtoH", err))
                return false;
            luma = Luma(spatial, w, h);
        }
    }
    const bool matched = chained && !luma.empty() && c->luma.size() == luma.size();   // the previous frame's luma is there

    // Motion: the motion vector AOV, else estimated, else none (still image).
    const float* flow = nullptr;
    if (chained && f.flow) {
        flow = f.flow;
        f.motion = DenoiseMotion::Aov;
    } else if (f.estimateFlow && matched) {
        if (EstimateFlow(luma, c->luma, w, h, g_planes.flow, f.flowNote)) {
            flow = g_planes.flow.data();
            f.motion = DenoiseMotion::Estimated;
        } else {
            static std::string logged;   // once per reason
            if (logged != f.flowNote) Log("denoise: no motion estimate: %s", (logged = f.flowNote).c_str());
        }
    }
    if (flow) {
        if (!CudaOk(g_cu.MemcpyHtoD(g_ox.flow, flow, flowBytes), "cuMemcpyHtoD", err)) return false;
    } else if (!CudaOk(g_cu.MemsetD8(g_ox.flow, 0, flowBytes), "cuMemsetD8", err)) {
        return false;
    }
    guide.flow = Plane(g_ox.flow, w, h, OPTIX_PIXEL_FORMAT_FLOAT2, 8);
    // Not given to OptiX as flowTrustworthiness: there it makes still areas flicker.
    const bool resolve = f.antiGhost && matched;
    if (resolve) Trust(luma, c->luma, flow, w, h, g_planes.trust);
    guide.previousOutputInternalGuideLayer = GuidePlane(c->guide[c->current], w, h);
    guide.outputInternalGuideLayer = GuidePlane(c->guide[1 - c->current], w, h);
    layer.previousOutput = Plane(c->output, w, h);   // read first, then overwritten with this frame
    layer.output = Plane(c->output, w, h);
    params.temporalModeUsePreviousLayers = chained ? 1 : 0;
    bool ok = Invoke(g_ox.temporal, params, guide, layer, err) &&
              CudaOk(g_cu.StreamSynchronize(g_ox.stream), "cuStreamSynchronize", err) &&
              CudaOk(g_cu.MemcpyDtoH(f.out, c->output, bytes), "cuMemcpyDtoH", err);
    if (ok && resolve) {
        // The next frame continues from the result without trails, so none can build up.
        Resolve(f.out, spatial, g_planes.trust, w, h);
        ok = CudaOk(g_cu.MemcpyHtoD(c->output, f.out, bytes), "cuMemcpyHtoD", err);
    }
    if (ok) {
        c->last = f.frame;
        c->current = 1 - c->current;
        c->luma = std::move(luma);
    }
    return ok;
}

}  // namespace

bool OptixBuiltIn() { return true; }

bool OptixDenoise(OptixFrame& f, std::string& device, std::string& err)
{
    if (!Init(err)) return false;
    device = g_ox.name;
    if (Run(f, err)) return true;
    ContextScope scope;
    DestroyDenoisers();   // start clean on the next frame
    return false;
}

void OptixRelease()
{
    if (!g_ox.optix) return;
    {
        ContextScope scope;
        DestroyDenoisers();
        g_of.unavailable.clear();   // tried again when the filter is turned on again
        g_of.badWidth = g_of.badHeight = 0;
        optixDeviceContextDestroy(g_ox.optix);
        g_ox.optix = nullptr;
        if (g_ox.stream) { g_cu.StreamDestroy(g_ox.stream); g_ox.stream = nullptr; }
    }
    g_cu.PrimaryCtxRelease(g_ox.device);
    g_ox.context = nullptr;
}

#else  // built without the OptiX headers

bool OptixBuiltIn() { return false; }

bool OptixDenoise(OptixFrame&, std::string&, std::string& err)
{
    err = "this build does not include the OptiX denoiser";
    return false;
}

void OptixRelease() {}

#endif
