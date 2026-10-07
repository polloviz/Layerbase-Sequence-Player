// Denoise filter: pixel conversion, the Open Image Denoise backend (loaded at run time from
// the downloaded libraries) and its installer. OptiX lives in DenoiseOptix.cpp.
#include "Denoise.h"
#include "Platform.h"

#include <windows.h>
#include <half.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <set>
#include <thread>
#include <vector>

const char* const kOidnVersion = "2.5.1";

namespace {

// Official Windows build (Apache 2.0), pinned and verified before anything is extracted.
const wchar_t* const kOidnUrl = L"https://github.com/RenderKit/oidn/releases/download/v2.5.1/oidn-2.5.1.x64.windows.zip";
const char* const kOidnSha256 = "F11F91BC072A5E3A564515724CB72AB8FCFBC445C84A84C197FF9B16CC01396F";
const wchar_t* const kOidnZipRoot = L"oidn-2.5.1.x64.windows";

// ---------------------------------------------------------------------------
// Pixels: the denoisers read and write packed float RGB in linear light.

enum class Plane { Color, Albedo, Normal };

float SrgbToLinear(float v)
{
    return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
}

float LinearToSrgb(float v)
{
    v = std::clamp(v, 0.0f, 1.0f);
    return v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
}

// Integer formats hold display-referred (sRGB) values: they are linearized for the denoiser and
// encoded back afterwards. Float data is scene-linear already.
std::vector<float> ToRgb(const Image& img, Plane plane)
{
    const size_t n = size_t(img.width) * img.height;
    std::vector<float> out(n * 3);
    auto store = [&](size_t i, int c, float v) {
        if (!std::isfinite(v)) v = 0.0f;
        if (plane == Plane::Color) v = std::max(v, 0.0f);
        else if (plane == Plane::Albedo) v = std::clamp(v, 0.0f, 1.0f);
        out[i * 3 + c] = v;
    };
    switch (img.type) {
    case PixelType::F16: {
        const half* p = reinterpret_cast<const half*>(img.data.data());
        for (size_t i = 0; i < n; ++i)
            for (int c = 0; c < 3; ++c) store(i, c, float(p[i * 4 + c]));
        break;
    }
    case PixelType::U8: {
        float lut[256];
        for (int v = 0; v < 256; ++v) lut[v] = plane == Plane::Normal ? v / 255.0f * 2.0f - 1.0f : SrgbToLinear(v / 255.0f);
        const uint8_t* p = img.data.data();
        for (size_t i = 0; i < n; ++i)
            for (int c = 0; c < 3; ++c) store(i, c, lut[p[i * 4 + c]]);
        break;
    }
    case PixelType::U16: {
        static const std::vector<float> lut = [] {
            std::vector<float> l(65536);
            for (int v = 0; v < 65536; ++v) l[v] = SrgbToLinear(v / 65535.0f);
            return l;
        }();
        const uint16_t* p = reinterpret_cast<const uint16_t*>(img.data.data());
        for (size_t i = 0; i < n; ++i)
            for (int c = 0; c < 3; ++c)
                store(i, c, plane == Plane::Normal ? p[i * 4 + c] / 65535.0f * 2.0f - 1.0f : lut[p[i * 4 + c]]);
        break;
    }
    }
    return out;
}

// Motion vectors as packed XY in pixels of this image, from the previous frame to this one.
std::vector<float> ToFlow(const Image& img, float scale, bool invert, bool flipY)
{
    const std::vector<float> v = ToRgb(img, Plane::Normal);   // signed values, first two channels used
    const size_t n = size_t(img.width) * img.height;
    std::vector<float> out(n * 2);
    const float sx = invert ? -scale : scale, sy = (invert != flipY) ? -scale : scale;
    for (size_t i = 0; i < n; ++i) {
        out[i * 2] = v[i * 3] * sx;
        out[i * 2 + 1] = v[i * 3 + 1] * sy;
    }
    return out;
}

// A copy of `src` (same type, alpha, metadata) with the RGB of `rgb`.
ImagePtr FromRgb(const Image& src, const std::vector<float>& rgb, const std::string& label)
{
    auto img = std::make_shared<Image>(src);
    img->description = src.description + "  \xC2\xB7  " + label;
    const size_t n = size_t(src.width) * src.height;
    switch (src.type) {
    case PixelType::F16: {
        half* p = reinterpret_cast<half*>(img->data.data());
        for (size_t i = 0; i < n; ++i)
            for (int c = 0; c < 3; ++c) p[i * 4 + c] = half(rgb[i * 3 + c]);
        break;
    }
    case PixelType::U8: {
        uint8_t* p = img->data.data();
        for (size_t i = 0; i < n; ++i)
            for (int c = 0; c < 3; ++c) p[i * 4 + c] = uint8_t(LinearToSrgb(rgb[i * 3 + c]) * 255.0f + 0.5f);
        break;
    }
    case PixelType::U16: {
        uint16_t* p = reinterpret_cast<uint16_t*>(img->data.data());
        for (size_t i = 0; i < n; ++i)
            for (int c = 0; c < 3; ++c) p[i * 4 + c] = uint16_t(LinearToSrgb(rgb[i * 3 + c]) * 65535.0f + 0.5f);
        break;
    }
    }
    return img;
}

// ---------------------------------------------------------------------------
// Open Image Denoise C API, resolved from the downloaded DLLs (see oidn.h of the release).

typedef struct OIDNDeviceImpl* OIDNDevice;
typedef struct OIDNFilterImpl* OIDNFilter;
typedef struct OIDNBufferImpl* OIDNBuffer;
enum { OIDN_DEVICE_TYPE_CPU = 1, OIDN_DEVICE_TYPE_SYCL = 2, OIDN_DEVICE_TYPE_CUDA = 3, OIDN_DEVICE_TYPE_HIP = 4 };
enum { OIDN_FORMAT_FLOAT3 = 3 };
enum { OIDN_QUALITY_FAST = 4, OIDN_QUALITY_BALANCED = 5, OIDN_QUALITY_HIGH = 6 };

struct OidnApi {
    HMODULE lib = nullptr;
    int (*GetNumPhysicalDevices)() = nullptr;
    int (*GetPhysicalDeviceInt)(int, const char*) = nullptr;
    const char* (*GetPhysicalDeviceString)(int, const char*) = nullptr;
    OIDNDevice (*NewDeviceByID)(int) = nullptr;
    void (*CommitDevice)(OIDNDevice) = nullptr;
    void (*ReleaseDevice)(OIDNDevice) = nullptr;
    int (*GetDeviceError)(OIDNDevice, const char**) = nullptr;
    OIDNBuffer (*NewBuffer)(OIDNDevice, size_t) = nullptr;
    void (*ReleaseBuffer)(OIDNBuffer) = nullptr;
    void (*WriteBuffer)(OIDNBuffer, size_t, size_t, const void*) = nullptr;
    void (*ReadBuffer)(OIDNBuffer, size_t, size_t, void*) = nullptr;
    OIDNFilter (*NewFilter)(OIDNDevice, const char*) = nullptr;
    void (*ReleaseFilter)(OIDNFilter) = nullptr;
    void (*SetFilterImage)(OIDNFilter, const char*, OIDNBuffer, int, size_t, size_t, size_t, size_t, size_t) = nullptr;
    void (*SetFilterBool)(OIDNFilter, const char*, bool) = nullptr;
    void (*SetFilterInt)(OIDNFilter, const char*, int) = nullptr;
    void (*CommitFilter)(OIDNFilter) = nullptr;
    void (*ExecuteFilter)(OIDNFilter) = nullptr;
};

// Everything below is used with g_lock held.
std::mutex g_lock;   // one frame at a time: the devices are shared
OidnApi g_oidn;
std::atomic<bool> g_oidnLoaded{ false };

struct OidnState {
    OIDNDevice device = nullptr;
    OidnDevice kind = OidnDevice::Auto;
    std::string name;
    OIDNFilter filter = nullptr;
    OIDNBuffer color = nullptr, albedo = nullptr, normal = nullptr, output = nullptr;
    int width = 0, height = 0;
    bool hasAlbedo = false, hasNormal = false;
    OidnQuality quality = OidnQuality::High;
} g_state;

template <class F>
bool Resolve(F& fn, const char* name)
{
    fn = reinterpret_cast<F>(GetProcAddress(g_oidn.lib, name));
    return fn != nullptr;
}

bool LoadOidn(std::string& err)
{
    if (g_oidn.lib) return true;
    const std::wstring dir = OidnDir();
    // Dependencies first, by full path: OIDN loads its device modules (and they load TBB / SYCL)
    // by name, which then resolve to the modules already in the process.
    for (const wchar_t* dep : { L"tbb12.dll", L"OpenImageDenoise_core.dll" })
        if (!LoadLibraryExW((dir + L"\\" + dep).c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH)) {
            err = "cannot load " + ToUtf8(dep) + " (error " + std::to_string(GetLastError()) + ")";
            return false;
        }
    HMODULE lib = LoadLibraryExW((dir + L"\\OpenImageDenoise.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!lib) {
        err = "cannot load OpenImageDenoise.dll (error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    g_oidn = OidnApi();
    g_oidn.lib = lib;
    const bool ok = Resolve(g_oidn.GetNumPhysicalDevices, "oidnGetNumPhysicalDevices") &&
                    Resolve(g_oidn.GetPhysicalDeviceInt, "oidnGetPhysicalDeviceInt") &&
                    Resolve(g_oidn.GetPhysicalDeviceString, "oidnGetPhysicalDeviceString") &&
                    Resolve(g_oidn.NewDeviceByID, "oidnNewDeviceByID") && Resolve(g_oidn.CommitDevice, "oidnCommitDevice") &&
                    Resolve(g_oidn.ReleaseDevice, "oidnReleaseDevice") && Resolve(g_oidn.GetDeviceError, "oidnGetDeviceError") &&
                    Resolve(g_oidn.NewBuffer, "oidnNewBuffer") && Resolve(g_oidn.ReleaseBuffer, "oidnReleaseBuffer") &&
                    Resolve(g_oidn.WriteBuffer, "oidnWriteBuffer") && Resolve(g_oidn.ReadBuffer, "oidnReadBuffer") &&
                    Resolve(g_oidn.NewFilter, "oidnNewFilter") && Resolve(g_oidn.ReleaseFilter, "oidnReleaseFilter") &&
                    Resolve(g_oidn.SetFilterImage, "oidnSetFilterImage") && Resolve(g_oidn.SetFilterBool, "oidnSetFilterBool") &&
                    Resolve(g_oidn.SetFilterInt, "oidnSetFilterInt") && Resolve(g_oidn.CommitFilter, "oidnCommitFilter") &&
                    Resolve(g_oidn.ExecuteFilter, "oidnExecuteFilter");
    if (!ok) {
        g_oidn = OidnApi();   // the module stays loaded; every attempt reports the same error
        err = "OpenImageDenoise.dll: unexpected version";
        return false;
    }
    g_oidnLoaded = true;
    Log("denoise: Open Image Denoise loaded from %s", ToUtf8(dir).c_str());
    return true;
}

bool OidnFailed(std::string& err)
{
    const char* msg = nullptr;
    if (!g_state.device || g_oidn.GetDeviceError(g_state.device, &msg) == 0) return false;
    err = msg && *msg ? msg : "Open Image Denoise error";
    return true;
}

void OidnReleaseFilter()
{
    for (OIDNBuffer* b : { &g_state.color, &g_state.albedo, &g_state.normal, &g_state.output })
        if (*b) { g_oidn.ReleaseBuffer(*b); *b = nullptr; }
    if (g_state.filter) { g_oidn.ReleaseFilter(g_state.filter); g_state.filter = nullptr; }
    g_state.width = g_state.height = 0;
}

void OidnReleaseAll()
{
    if (!g_oidn.lib) return;
    OidnReleaseFilter();
    if (g_state.device) { g_oidn.ReleaseDevice(g_state.device); g_state.device = nullptr; }
    g_state.name.clear();
}

bool OidnOpenDevice(OidnDevice kind, std::string& err)
{
    if (g_state.device && g_state.kind == kind) return true;
    OidnReleaseAll();
    const int n = g_oidn.GetNumPhysicalDevices();
    int pick = -1;
    for (int i = 0; i < n && pick < 0; ++i) {
        const int type = g_oidn.GetPhysicalDeviceInt(i, "type");
        // Physical devices come fastest first: device 0 is OIDN's own default choice.
        if (kind == OidnDevice::Auto || (kind == OidnDevice::Cpu) == (type == OIDN_DEVICE_TYPE_CPU)) pick = i;
    }
    if (pick < 0) {
        err = kind == OidnDevice::Gpu ? "no GPU supported by Open Image Denoise was found" : "no supported device";
        return false;
    }
    OIDNDevice dev = g_oidn.NewDeviceByID(pick);
    if (!dev) { err = "cannot create the device"; return false; }
    g_state.device = dev;
    g_oidn.CommitDevice(dev);
    if (OidnFailed(err)) { OidnReleaseAll(); return false; }
    g_state.kind = kind;
    const int type = g_oidn.GetPhysicalDeviceInt(pick, "type");
    const char* name = g_oidn.GetPhysicalDeviceString(pick, "name");
    const char* api = type == OIDN_DEVICE_TYPE_CUDA ? "CUDA" : type == OIDN_DEVICE_TYPE_HIP ? "HIP"
                    : type == OIDN_DEVICE_TYPE_SYCL ? "SYCL" : "CPU";
    g_state.name = std::string(name && *name ? name : "?") + " (" + api + ")";
    Log("denoise: OIDN device %s", g_state.name.c_str());
    return true;
}

bool OidnDenoise(const float* rgb, const float* albedo, const float* normal, int w, int h, const DenoiseSpec& spec,
                 float* out, std::string& device, std::string& err)
{
    if (!LoadOidn(err) || !OidnOpenDevice(spec.device, err)) return false;
    device = g_state.name;
    const size_t bytes = size_t(w) * h * 3 * sizeof(float);
    if (!g_state.filter || g_state.width != w || g_state.height != h || g_state.hasAlbedo != (albedo != nullptr) ||
        g_state.hasNormal != (normal != nullptr) || g_state.quality != spec.quality) {
        OidnReleaseFilter();
        g_state.filter = g_oidn.NewFilter(g_state.device, "RT");   // generic ray tracing filter
        g_state.color = g_oidn.NewBuffer(g_state.device, bytes);
        g_state.output = g_oidn.NewBuffer(g_state.device, bytes);
        if (albedo) g_state.albedo = g_oidn.NewBuffer(g_state.device, bytes);
        if (normal) g_state.normal = g_oidn.NewBuffer(g_state.device, bytes);
        if (OidnFailed(err) || !g_state.filter) { OidnReleaseFilter(); return false; }
        g_oidn.SetFilterImage(g_state.filter, "color", g_state.color, OIDN_FORMAT_FLOAT3, w, h, 0, 0, 0);
        if (albedo) g_oidn.SetFilterImage(g_state.filter, "albedo", g_state.albedo, OIDN_FORMAT_FLOAT3, w, h, 0, 0, 0);
        if (normal) g_oidn.SetFilterImage(g_state.filter, "normal", g_state.normal, OIDN_FORMAT_FLOAT3, w, h, 0, 0, 0);
        g_oidn.SetFilterImage(g_state.filter, "output", g_state.output, OIDN_FORMAT_FLOAT3, w, h, 0, 0, 0);
        g_oidn.SetFilterBool(g_state.filter, "hdr", true);   // the input is always linear (see ToRgb)
        g_oidn.SetFilterInt(g_state.filter, "quality", spec.quality == OidnQuality::Fast ? OIDN_QUALITY_FAST
                                                       : spec.quality == OidnQuality::Balanced ? OIDN_QUALITY_BALANCED
                                                                                               : OIDN_QUALITY_HIGH);
        g_oidn.CommitFilter(g_state.filter);
        if (OidnFailed(err)) { OidnReleaseFilter(); return false; }
        g_state.width = w;
        g_state.height = h;
        g_state.hasAlbedo = albedo != nullptr;
        g_state.hasNormal = normal != nullptr;
        g_state.quality = spec.quality;
    }
    g_oidn.WriteBuffer(g_state.color, 0, bytes, rgb);
    if (albedo) g_oidn.WriteBuffer(g_state.albedo, 0, bytes, albedo);
    if (normal) g_oidn.WriteBuffer(g_state.normal, 0, bytes, normal);
    g_oidn.ExecuteFilter(g_state.filter);
    g_oidn.ReadBuffer(g_state.output, 0, bytes, out);
    if (OidnFailed(err)) { OidnReleaseFilter(); return false; }
    return true;
}

// ---------------------------------------------------------------------------

std::mutex g_statusMutex;
DenoiseStatus g_status;
// Bumped when the filter is switched off: frames still waiting for the lock are dropped
// (their decode result is discarded anyway) instead of opening the device again.
std::atomic<int> g_epoch{ 0 };

// Temporal mode: frames being decoded now, per stream (see DenoiseFrameStarted).
std::mutex g_orderMutex;
std::condition_variable g_orderCv;
std::set<std::pair<std::string, int>> g_inProgress;

std::wstring SystemDir()
{
    wchar_t buf[MAX_PATH] = {};
    GetSystemDirectoryW(buf, MAX_PATH);
    return buf;
}

bool RemoveTree(const std::wstring& dir)
{
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring name = fd.cFileName;
            if (name == L"." || name == L"..") continue;
            const std::wstring p = dir + L"\\" + name;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) RemoveTree(p);
            else {
                SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(p.c_str());
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return RemoveDirectoryW(dir.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND;
}

// Windows' bsdtar (Windows 10 1803 and later) reads zip files.
bool ExtractZip(const std::wstring& zip, const std::wstring& dest, std::string& err)
{
    const std::wstring tar = SystemDir() + L"\\tar.exe";
    if (!FileExists(tar)) { err = "tar.exe not found (Windows 10 1803 or later is required)"; return false; }
    std::wstring cmd = L"\"" + tar + L"\" -xf \"" + zip + L"\" -C \"" + dest + L"\"";
    STARTUPINFOW si{ sizeof(si) };
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(tar.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, dest.c_str(), &si, &pi)) {
        err = "cannot run tar.exe";
        return false;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (code != 0) { err = "extraction failed (tar " + std::to_string(code) + ")"; return false; }
    return true;
}

std::atomic<int> g_installedCache{ -1 };   // -1 unknown

void RunOidnInstall(const std::shared_ptr<OidnInstall>& job, const std::function<void()>& wake)
{
    std::string err;
    const std::wstring dir = OidnDir();
    const std::wstring work = dir + L".download";
    RemoveTree(work);
    CreateDirectoryW(work.c_str(), nullptr);
    const std::wstring zip = work + L"\\oidn.zip";
    auto lastWake = std::chrono::steady_clock::now();
    bool ok = HttpDownload(kOidnUrl, zip, [&](uint64_t got, uint64_t total) {
        job->received = got;
        job->total = total;
        const auto now = std::chrono::steady_clock::now();
        if (wake && now - lastWake > std::chrono::milliseconds(100)) {
            lastWake = now;
            wake();
        }
        return !job->cancel.load();
    }, err);
    if (ok) {
        job->stage = 1;
        if (wake) wake();
        if (FileSha256(zip) != kOidnSha256) { ok = false; err = "checksum mismatch: the download is not the expected release"; }
    }
    if (ok && !job->cancel) {
        job->stage = 2;
        if (wake) wake();
        ok = ExtractZip(zip, work, err);
    }
    if (ok && !job->cancel) {
        // Keep the libraries and their licenses; skip the samples, headers and import libraries.
        const std::wstring root = work + L"\\" + kOidnZipRoot, staged = work + L"\\lib";
        CreateDirectoryW(staged.c_str(), nullptr);
        auto moveAll = [&](const std::wstring& from, const wchar_t* pattern) {
            WIN32_FIND_DATAW fd;
            HANDLE h = FindFirstFileW((from + L"\\" + pattern).c_str(), &fd);
            if (h == INVALID_HANDLE_VALUE) return;
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                    MoveFileExW((from + L"\\" + fd.cFileName).c_str(), (staged + L"\\" + fd.cFileName).c_str(), MOVEFILE_REPLACE_EXISTING);
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        };
        moveAll(root + L"\\bin", L"*.dll");
        moveAll(root + L"\\doc", L"LICENSE.txt");
        moveAll(root + L"\\doc", L"third-party-programs*.txt");
        RemoveTree(dir);
        if (!FileExists(staged + L"\\OpenImageDenoise.dll")) { ok = false; err = "unexpected archive layout"; }
        else if (!MoveFileExW(staged.c_str(), dir.c_str(), 0)) { ok = false; err = "cannot create " + ToUtf8(dir); }
    }
    if (job->cancel && ok) { ok = false; err = "cancelled"; }
    RemoveTree(work);
    g_installedCache = -1;
    Log("denoise: OIDN install %s %s", ok ? "done" : "failed:", err.c_str());
    {
        std::lock_guard lock(job->mutex);
        job->error = err;
    }
    job->ok = ok;
    job->done = true;
    if (wake) wake();
}

}  // namespace

// ---------------------------------------------------------------------------

ImagePtr DenoiseImage(const DenoiseInput& in, const DenoiseSpec& spec)
{
    const ImagePtr& color = in.color;
    if (!color || !color->valid()) return color;
    const int w = color->width, h = color->height;
    auto fits = [&](const ImagePtr& g) { return g && g->valid() && g->width == w && g->height == h; };
    const bool temporal = spec.temporal && spec.engine == DenoiseEngine::Optix && in.frame >= 0;
    const bool useAlbedo = fits(in.albedo), useNormal = useAlbedo && fits(in.normal);   // a normal guide needs albedo
    const bool useFlow = temporal && fits(in.flow);
    const std::vector<float> rgb = ToRgb(*color, Plane::Color);
    const std::vector<float> alb = useAlbedo ? ToRgb(*in.albedo, Plane::Albedo) : std::vector<float>();
    const std::vector<float> nrm = useNormal ? ToRgb(*in.normal, Plane::Normal) : std::vector<float>();
    const std::vector<float> flow = useFlow ? ToFlow(*in.flow, in.flowScale, spec.flowInvert, spec.flowFlipY) : std::vector<float>();
    std::vector<float> out(rgb.size());

    const int epoch = g_epoch;
    if (temporal && in.frame > 0) {
        // Follow the frame order: while the previous frame is still being decoded, wait for it.
        std::unique_lock l(g_orderMutex);
        g_orderCv.wait_for(l, std::chrono::seconds(10), [&] {
            return epoch != g_epoch || !g_inProgress.count({ in.stream, in.frame - 1 });
        });
    }

    std::string device, err;
    bool ok = false, chained = false;
    DenoiseMotion motion = DenoiseMotion::None;
    std::string flowNote;
    double ms = 0;
    {
        std::lock_guard lock(g_lock);
        if (epoch != g_epoch) return color;
        {
            std::lock_guard s(g_statusMutex);
            g_status.busy = true;
        }
        const auto t0 = std::chrono::steady_clock::now();
        if (spec.engine == DenoiseEngine::Optix) {
            OptixFrame f;
            f.rgb = rgb.data();
            f.albedo = useAlbedo ? alb.data() : nullptr;
            f.normal = useNormal ? nrm.data() : nullptr;
            f.flow = useFlow ? flow.data() : nullptr;
            f.width = w;
            f.height = h;
            f.temporal = temporal;
            f.estimateFlow = temporal && !useFlow && spec.estimateFlow;
            f.antiGhost = temporal && spec.antiGhost;
            f.stream = &in.stream;
            f.frame = in.frame;
            f.out = out.data();
            ok = OptixDenoise(f, device, err);
            chained = f.chained;
            motion = f.motion;
            flowNote = std::move(f.flowNote);
        } else {
            ok = OidnDenoise(rgb.data(), useAlbedo ? alb.data() : nullptr, useNormal ? nrm.data() : nullptr, w, h, spec, out.data(), device, err);
        }
        ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    }
    {
        std::lock_guard s(g_statusMutex);
        g_status.busy = false;
        if (!device.empty()) g_status.device = device;
        if (ok) {
            g_status.error.clear();
            g_status.lastMs = ms;
            ++g_status.frames;
            if (chained) {   // the first frame of a chain has no previous one to follow
                g_status.motion = motion;
                g_status.flowNote = flowNote;
            }
        } else {
            g_status.error = err;
        }
    }
    const char* engine = spec.engine == DenoiseEngine::Optix ? (temporal ? "OptiX temporal" : "OptiX") : "OIDN";
    if (!ok) {
        Log("denoise: %s failed: %s", engine, err.c_str());
        auto img = std::make_shared<Image>();
        img->error = std::string("Denoise (") + engine + "): " + err;
        return img;
    }
    std::string label = std::string("denoised ") + engine;
    if (useAlbedo) label += useNormal ? " +albedo +normal" : " +albedo";
    if (motion == DenoiseMotion::Aov) label += " +motion vectors";
    else if (motion == DenoiseMotion::Estimated) label += " +estimated motion";
    // Strength: back towards the original frame. The temporal chain keeps the full result.
    if (spec.strength < 1.0f) {
        const float k = std::clamp(spec.strength, 0.0f, 1.0f);
        for (size_t i = 0; i < out.size(); ++i) out[i] = rgb[i] + (out[i] - rgb[i]) * k;
        label += " " + std::to_string(int(k * 100.0f + 0.5f)) + "%";
    }
    return FromRgb(*color, out, label);
}

void DenoiseFrameStarted(const std::string& stream, int frame)
{
    std::lock_guard l(g_orderMutex);
    g_inProgress.insert({ stream, frame });
}

void DenoiseFrameFinished(const std::string& stream, int frame)
{
    {
        std::lock_guard l(g_orderMutex);
        g_inProgress.erase({ stream, frame });
    }
    g_orderCv.notify_all();
}

DenoiseStatus GetDenoiseStatus()
{
    std::lock_guard s(g_statusMutex);
    return g_status;
}

void ReleaseDenoisers()
{
    ++g_epoch;
    g_orderCv.notify_all();
    std::thread([] {
        std::lock_guard lock(g_lock);
        OidnReleaseAll();
        OptixRelease();
        std::lock_guard s(g_statusMutex);
        g_status = DenoiseStatus();
    }).detach();
}

std::wstring OidnDir()
{
    static const std::wstring dir = GetLocalDataDir() + L"\\oidn-" + FromUtf8(kOidnVersion);
    return dir;
}

bool OidnInstalled()
{
    int v = g_installedCache;
    if (v < 0) {
        const std::wstring d = OidnDir();
        v = FileExists(d + L"\\OpenImageDenoise.dll") && FileExists(d + L"\\OpenImageDenoise_core.dll") &&
            FileExists(d + L"\\OpenImageDenoise_device_cpu.dll") && FileExists(d + L"\\tbb12.dll");
        g_installedCache = v;
    }
    return v == 1;
}

bool OidnLoaded() { return g_oidnLoaded; }

bool RemoveOidn(std::string& err)
{
    if (OidnLoaded()) { err = "in use: restart the program first"; return false; }
    const bool ok = RemoveTree(OidnDir());
    g_installedCache = -1;
    if (!ok) err = "cannot remove " + ToUtf8(OidnDir());
    return ok;
}

std::shared_ptr<OidnInstall> StartOidnInstall(std::function<void()> wake)
{
    auto job = std::make_shared<OidnInstall>();
    std::thread([job, wake = std::move(wake)] { RunOidnInstall(job, wake); }).detach();
    return job;
}

bool NvidiaDriverPresent()
{
    static const bool present = FileExists(SystemDir() + L"\\nvcuda.dll");
    return present;
}
