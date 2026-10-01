#pragma once
#include <functional>
#include <string>
#include <thread>

// Watches a folder from a background thread: onChange runs (on that thread) whenever
// files are added, removed, renamed or written. Costs one idle thread while watching.
class DirWatcher {
public:
    ~DirWatcher() { stop(); }
    void start(const std::wstring& dir, std::function<void()> onChange);
    void stop();
    const std::wstring& dir() const { return m_dir; }

private:
    std::thread m_thread;
    void* m_stop = nullptr;   // event
    std::wstring m_dir;
};
