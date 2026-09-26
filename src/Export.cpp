#include "Export.h"
#include "Platform.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <map>

const char* ExportCodecName(ExportCodec c)
{
    switch (c) {
    case ExportCodec::H264: return "H.264 (MP4)";
    case ExportCodec::H265: return "H.265 / HEVC (MP4)";
    case ExportCodec::ProRes422Proxy: return "ProRes 422 Proxy (MOV)";
    case ExportCodec::ProRes422LT: return "ProRes 422 LT (MOV)";
    case ExportCodec::ProRes422: return "ProRes 422 (MOV)";
    case ExportCodec::ProRes422HQ: return "ProRes 422 HQ (MOV)";
    case ExportCodec::ProRes4444: return "ProRes 4444 (MOV)";
    default: return "?";
    }
}

bool ExportCodecIsProRes(ExportCodec c) { return c >= ExportCodec::ProRes422Proxy && c <= ExportCodec::ProRes4444; }
const wchar_t* ExportCodecExtension(ExportCodec c) { return ExportCodecIsProRes(c) ? L".mov" : L".mp4"; }
bool ExportCodecWants16Bit(ExportCodec c) { return c != ExportCodec::H264; }

// ---------------------------------------------------------------------------
// FFmpeg discovery

static std::string RunCapture(const std::wstring& exe, const std::wstring& args)
{
    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return {};
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{ sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    si.hStdError = wr;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + exe + L"\" " + args;
    std::string out;
    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(wr);
        wr = nullptr;
        char buf[4096];
        DWORD n = 0;
        while (ReadFile(rd, buf, sizeof(buf), &n, nullptr) && n > 0) out.append(buf, n);
        WaitForSingleObject(pi.hProcess, 5000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    if (wr) CloseHandle(wr);
    CloseHandle(rd);
    return out;
}

FFmpegInfo FindFFmpeg(const std::wstring& preferred)
{
    static std::mutex mtx;
    static std::map<std::wstring, FFmpegInfo> cache;

    std::vector<std::wstring> candidates;
    if (!preferred.empty()) candidates.push_back(preferred);
    candidates.push_back(GetParentDir(GetExePath()) + L"\\ffmpeg.exe");
    wchar_t found[MAX_PATH];
    if (SearchPathW(nullptr, L"ffmpeg.exe", nullptr, MAX_PATH, found, nullptr)) candidates.push_back(found);
    wchar_t local[MAX_PATH];
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH))
        candidates.push_back(std::wstring(local) + L"\\Microsoft\\WinGet\\Links\\ffmpeg.exe");
    for (const wchar_t* p : { L"C:\\FFMPEG\\bin\\ffmpeg.exe", L"C:\\ffmpeg\\ffmpeg.exe", L"C:\\Program Files\\ffmpeg\\bin\\ffmpeg.exe",
                              L"C:\\ProgramData\\chocolatey\\bin\\ffmpeg.exe" })
        candidates.push_back(p);

    for (const auto& c : candidates) {
        if (!FileExists(c)) continue;
        std::lock_guard lock(mtx);
        auto it = cache.find(c);
        if (it != cache.end()) return it->second;
        const std::string enc = RunCapture(c, L"-hide_banner -encoders");
        if (enc.find("Encoders:") == std::string::npos) continue;
        FFmpegInfo info;
        info.path = c;
        auto has = [&](const char* name) { return enc.find(std::string(" ") + name + " ") != std::string::npos; };
        info.libx264 = has("libx264");
        info.libx265 = has("libx265");
        info.nvencH264 = has("h264_nvenc");
        info.nvencH265 = has("hevc_nvenc");
        info.mfH264 = has("h264_mf");
        info.mfH265 = has("hevc_mf");
        info.prores = has("prores_ks");
        cache[c] = info;
        return info;
    }
    return {};
}

// ---------------------------------------------------------------------------
// Exporter

static std::wstring FpsArg(double fps)
{
    static const struct { double f; const wchar_t* s; } ntsc[] = {
        { 23.976, L"24000/1001" }, { 29.97, L"30000/1001" }, { 47.952, L"48000/1001" }, { 59.94, L"60000/1001" }, { 119.88, L"120000/1001" },
    };
    for (auto& n : ntsc)
        if (std::fabs(fps - n.f) < 0.002) return n.s;
    wchar_t buf[32];
    swprintf(buf, 32, L"%.6g", fps);
    return buf;
}

MovieExporter::~MovieExporter()
{
    if (m_running) cancel();
    if (m_thread.joinable()) m_thread.join();
    if (m_process) CloseHandle(m_process);
}

bool MovieExporter::start(const FFmpegInfo& ff, const ExportOptions& opt, std::string& err)
{
    if (!ff.found()) { err = "FFmpeg not found"; return false; }
    m_16bit = ExportCodecWants16Bit(opt.codec);
    m_alpha = opt.alpha && opt.codec == ExportCodec::ProRes4444;
    m_outputPath = opt.outputPath;

    auto even = [](double v) { return std::max(2, (int)std::lround(v / 2.0) * 2); };
    const int outW = even(opt.width * opt.scalePercent / 100.0), outH = even(opt.height * opt.scalePercent / 100.0);
    const int q = (int)opt.quality;

    std::wstring pixOut, codec;
    switch (opt.codec) {
    case ExportCodec::H264:
        pixOut = L"yuv420p";
        if (opt.hardware && ff.nvencH264)
            codec = L"-c:v h264_nvenc -preset p6 -tune hq -rc vbr -cq " + std::to_wstring(17 + q * 5) + L" -b:v 0 -profile:v high";
        else if (ff.libx264)
            codec = L"-c:v libx264 -preset slow -crf " + std::to_wstring(16 + q * 4) + L" -profile:v high";
        else if (ff.mfH264)
            codec = L"-c:v h264_mf -b:v " + std::to_wstring(40 >> q) + L"M";
        else { err = "No H.264 encoder in this FFmpeg build"; return false; }
        codec += L" -movflags +faststart";
        break;
    case ExportCodec::H265:
        if (opt.hardware && ff.nvencH265) {
            pixOut = L"p010le";
            codec = L"-c:v hevc_nvenc -preset p6 -tune hq -rc vbr -cq " + std::to_wstring(19 + q * 5) + L" -b:v 0";
        } else if (ff.libx265) {
            pixOut = L"yuv420p10le";
            codec = L"-c:v libx265 -preset medium -crf " + std::to_wstring(18 + q * 4) + L" -x265-params log-level=error";
        } else if (ff.mfH265) {
            pixOut = L"nv12";
            codec = L"-c:v hevc_mf -b:v " + std::to_wstring(30 >> q) + L"M";
        } else { err = "No H.265 encoder in this FFmpeg build"; return false; }
        codec += L" -tag:v hvc1 -movflags +faststart";
        break;
    default: {
        if (!ff.prores) { err = "No ProRes encoder in this FFmpeg build"; return false; }
        const int profile = (int)opt.codec - (int)ExportCodec::ProRes422Proxy;   // 0 proxy .. 4 4444
        pixOut = opt.codec == ExportCodec::ProRes4444 ? (m_alpha ? L"yuva444p10le" : L"yuv444p10le") : L"yuv422p10le";
        codec = L"-c:v prores_ks -profile:v " + std::to_wstring(profile) + L" -vendor apl0";
        break;
    }
    }

    const std::wstring swsMatrix = opt.matrix == "bt2020nc" ? L"bt2020" : L"bt709";
    std::wstring args = L"-hide_banner -loglevel warning -y -f rawvideo -pix_fmt ";
    args += m_alpha ? L"rgba64le" : m_16bit ? L"rgb48le" : L"rgb24";
    args += L" -s " + std::to_wstring(opt.width) + L"x" + std::to_wstring(opt.height);
    args += L" -framerate " + FpsArg(opt.fps) + L" -i pipe:0 -an";
    args += L" -vf \"scale=" + std::to_wstring(outW) + L":" + std::to_wstring(outH) +
            L":flags=lanczos+accurate_rnd+full_chroma_int:out_color_matrix=" + swsMatrix + L":out_range=tv,format=" + pixOut +
            // Recent FFmpeg takes color tags from the frames, so set them in the graph too.
            L",setparams=range=tv:color_primaries=" + FromUtf8(opt.primaries) + L":color_trc=" + FromUtf8(opt.transfer) +
            L":colorspace=" + FromUtf8(opt.matrix) + L"\"";
    args += L" " + codec;
    args += L" -color_primaries " + FromUtf8(opt.primaries) + L" -color_trc " + FromUtf8(opt.transfer) +
            L" -colorspace " + FromUtf8(opt.matrix) + L" -color_range tv";
    args += L" \"" + opt.outputPath + L"\"";

    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    m_logPath = std::wstring(tmp) + L"SequencePlayer_ffmpeg.log";

    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE log = CreateFileW(m_logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS, 0, nullptr);
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 8u << 20)) { err = "CreatePipe failed"; if (log != INVALID_HANDLE_VALUE) CloseHandle(log); return false; }
    SetHandleInformation(wr, HANDLE_FLAG_INHERIT, 0);
    if (log != INVALID_HANDLE_VALUE) {
        DWORD n;
        const std::string header = "ffmpeg " + ToUtf8(args) + "\r\n\r\n";
        WriteFile(log, header.data(), (DWORD)header.size(), &n, nullptr);
    }

    STARTUPINFOW si{ sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = rd;
    si.hStdOutput = log;
    si.hStdError = log;
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + ff.path + L"\" " + args;
    const BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS,
                                   nullptr, nullptr, &si, &pi);
    CloseHandle(rd);
    if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
    if (!ok) {
        CloseHandle(wr);
        err = "Cannot start FFmpeg";
        return false;
    }
    CloseHandle(pi.hThread);
    m_process = pi.hProcess;
    m_stdinWrite = wr;
    m_running = true;
    m_succeeded = false;
    m_written = 0;
    m_thread = std::thread([this] { writerLoop(); });
    return true;
}

bool MovieExporter::canPush() const
{
    std::lock_guard lock(m_mutex);
    return m_running && !m_finishing && m_error.empty() && m_queue.size() < 3;
}

void MovieExporter::push(std::vector<uint8_t>&& frame)
{
    {
        std::lock_guard lock(m_mutex);
        m_queue.push_back(std::move(frame));
    }
    m_cv.notify_one();
}

void MovieExporter::finish()
{
    {
        std::lock_guard lock(m_mutex);
        m_finishing = true;
    }
    m_cv.notify_one();
}

void MovieExporter::cancel()
{
    {
        std::lock_guard lock(m_mutex);
        m_cancelled = true;
        m_queue.clear();
    }
    if (m_process) TerminateProcess(m_process, 1);
    m_cv.notify_one();
    if (m_thread.joinable()) m_thread.join();
    DeleteFileW(m_outputPath.c_str());
}

std::string MovieExporter::error() const
{
    std::lock_guard lock(m_mutex);
    return m_error;
}

std::string MovieExporter::readLogTail() const
{
    HANDLE h = CreateFileW(m_logPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    LARGE_INTEGER size;
    GetFileSizeEx(h, &size);
    const LONGLONG start = std::max<LONGLONG>(0, size.QuadPart - 600);
    LARGE_INTEGER pos;
    pos.QuadPart = start;
    SetFilePointerEx(h, pos, nullptr, FILE_BEGIN);
    std::string s(size_t(size.QuadPart - start), '\0');
    DWORD n = 0;
    ReadFile(h, s.data(), (DWORD)s.size(), &n, nullptr);
    CloseHandle(h);
    s.resize(n);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    const size_t cut = s.rfind('\n', s.size() > 300 ? s.size() - 300 : 0);
    return cut == std::string::npos ? s : s.substr(cut + 1);
}

void MovieExporter::writerLoop()
{
    for (;;) {
        std::vector<uint8_t> frame;
        {
            std::unique_lock lock(m_mutex);
            m_cv.wait(lock, [&] { return m_cancelled || m_finishing || !m_queue.empty(); });
            if (m_cancelled) break;
            if (m_queue.empty() && m_finishing) break;
            frame = std::move(m_queue.front());
            m_queue.pop_front();
        }
        size_t done = 0;
        while (done < frame.size()) {
            DWORD n = 0;
            const DWORD chunk = (DWORD)std::min<size_t>(frame.size() - done, 1u << 24);
            if (!WriteFile(m_stdinWrite, frame.data() + done, chunk, &n, nullptr) || n == 0) break;
            done += n;
        }
        if (done < frame.size()) {   // ffmpeg exited early
            std::lock_guard lock(m_mutex);
            if (!m_cancelled) m_error = "FFmpeg stopped: " + readLogTail();
            break;
        }
        ++m_written;
    }

    CloseHandle(m_stdinWrite);
    m_stdinWrite = nullptr;
    WaitForSingleObject(m_process, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(m_process, &code);
    {
        std::lock_guard lock(m_mutex);
        if (!m_cancelled && code != 0 && m_error.empty()) m_error = "FFmpeg error: " + readLogTail();
        m_succeeded = !m_cancelled && code == 0 && m_error.empty();
    }
    m_running = false;
}
