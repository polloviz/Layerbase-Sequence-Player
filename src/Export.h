#pragma once
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

enum class ExportCodec : int { H264 = 0, H265, ProRes422Proxy, ProRes422LT, ProRes422, ProRes422HQ, ProRes4444, Count };
enum class ExportQuality : int { High = 0, Medium, Low };

struct ExportOptions {
    std::wstring outputPath;
    ExportCodec codec = ExportCodec::H264;
    ExportQuality quality = ExportQuality::High;
    bool hardware = false;        // NVENC for H.264 / H.265
    bool alpha = false;           // ProRes 4444 only: embed alpha (image alpha / Cryptomatte mask)
    int width = 0, height = 0;    // source frame size
    int scalePercent = 100;       // 100 / 50 / 25
    double fps = 30.0;
    // Color tagging of the display-referred output (derived from the OCIO display).
    std::string primaries = "bt709", transfer = "bt709", matrix = "bt709";
};

const char* ExportCodecName(ExportCodec c);
const wchar_t* ExportCodecExtension(ExportCodec c);   // L".mp4" / L".mov"
bool ExportCodecIsProRes(ExportCodec c);
bool ExportCodecWants16Bit(ExportCodec c);           // 10-bit targets get 16-bit input

struct FFmpegInfo {
    std::wstring path;
    bool libx264 = false, libx265 = false, nvencH264 = false, nvencH265 = false, prores = false, mfH264 = false, mfH265 = false;
    bool found() const { return !path.empty(); }
};

// Locates ffmpeg.exe (explicit path, next to the exe, PATH, common folders) and probes encoders.
FFmpegInfo FindFFmpeg(const std::wstring& preferred);

// Feeds raw RGB frames to an ffmpeg process through a pipe on a writer thread.
class MovieExporter {
public:
    ~MovieExporter();
    bool start(const FFmpegInfo& ff, const ExportOptions& opt, std::string& err);
    bool canPush() const;                          // false while the queue is full
    void push(std::vector<uint8_t>&& frame);
    void finish();                                 // no more frames: close pipe, wait in background
    void cancel();                                 // kill ffmpeg, delete partial file

    bool running() const { return m_running; }     // process alive (also while finishing)
    bool succeeded() const { return m_succeeded; }
    std::string error() const;
    int framesWritten() const { return m_written; }
    const std::wstring& logPath() const { return m_logPath; }
    bool is16Bit() const { return m_16bit; }
    bool hasAlpha() const { return m_alpha; }

private:
    void writerLoop();
    std::string readLogTail() const;

    void* m_process = nullptr;
    void* m_stdinWrite = nullptr;
    std::thread m_thread;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<std::vector<uint8_t>> m_queue;
    bool m_finishing = false, m_cancelled = false, m_16bit = false, m_alpha = false;
    std::atomic<bool> m_running{ false }, m_succeeded{ false };
    std::atomic<int> m_written{ 0 };
    std::string m_error;
    std::wstring m_logPath, m_outputPath;
};
