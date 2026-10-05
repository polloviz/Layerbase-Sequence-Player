// NVIDIA OptiX denoiser. OptiX and CUDA ship with the NVIDIA driver (nvoptix.dll, nvcuda.dll):
// both are loaded on the first denoised frame and nothing is downloaded. The OptiX SDK headers
// are fetched at build time (tools/get_optix.ps1); a build without them reports the engine as
// unavailable.
#include "Denoise.h"
#include "Platform.h"

#ifdef SP_HAVE_OPTIX

#include <optix.h>
#include <optix_stubs.h>
#include <optix_function_table_definition.h>

#include <windows.h>

#include <algorithm>
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
};
std::vector<Chain> g_chains;
uint64_t g_chainClock = 0;
constexpr size_t kMaxChains = 6;   // e.g. 3 stack layers, each wrapping around

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

void DestroyDenoisers()
{
    FreeBuffers();
    FreeChains();
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

bool Run(const OptixFrame& f, std::string& err)
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
    if (!chained) {
        // A chain starts: its "previous" frame is this one denoised on its own (cleaner than the
        // noisy frame the documentation allows), with zero motion and cleared guides.
        Log("denoise: temporal chain starts at frame %d", f.frame);
        layer.output = Plane(c->output, w, h);
        if (!Invoke(g_ox.spatial, params, guide, layer, err) ||
            !CudaOk(g_cu.MemsetD8(c->guide[c->current], 0, size_t(w) * h * g_ox.guidePixelSize), "cuMemsetD8", err))
            return false;
    }
    if (chained && f.flow) {
        if (!CudaOk(g_cu.MemcpyHtoD(g_ox.flow, f.flow, flowBytes), "cuMemcpyHtoD", err)) return false;
    } else if (!CudaOk(g_cu.MemsetD8(g_ox.flow, 0, flowBytes), "cuMemsetD8", err)) {
        return false;
    }
    guide.flow = Plane(g_ox.flow, w, h, OPTIX_PIXEL_FORMAT_FLOAT2, 8);
    guide.previousOutputInternalGuideLayer = GuidePlane(c->guide[c->current], w, h);
    guide.outputInternalGuideLayer = GuidePlane(c->guide[1 - c->current], w, h);
    layer.previousOutput = Plane(c->output, w, h);   // read first, then overwritten with this frame
    layer.output = Plane(c->output, w, h);
    params.temporalModeUsePreviousLayers = chained ? 1 : 0;
    c->last = -1;   // until this frame succeeds
    const bool ok = Invoke(g_ox.temporal, params, guide, layer, err) &&
                    CudaOk(g_cu.StreamSynchronize(g_ox.stream), "cuStreamSynchronize", err) &&
                    CudaOk(g_cu.MemcpyDtoH(f.out, c->output, bytes), "cuMemcpyDtoH", err);
    if (ok) {
        c->last = f.frame;
        c->current = 1 - c->current;
    }
    return ok;
}

}  // namespace

bool OptixBuiltIn() { return true; }

bool OptixDenoise(const OptixFrame& f, std::string& device, std::string& err)
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
        optixDeviceContextDestroy(g_ox.optix);
        g_ox.optix = nullptr;
        if (g_ox.stream) { g_cu.StreamDestroy(g_ox.stream); g_ox.stream = nullptr; }
    }
    g_cu.PrimaryCtxRelease(g_ox.device);
    g_ox.context = nullptr;
}

#else  // built without the OptiX headers

bool OptixBuiltIn() { return false; }

bool OptixDenoise(const OptixFrame&, std::string&, std::string& err)
{
    err = "this build does not include the OptiX denoiser";
    return false;
}

void OptixRelease() {}

#endif
