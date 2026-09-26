#pragma once
#include "ExrLayers.h"
#include "Image.h"
#include "Sequence.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>
#include <vector>

// Background multi-threaded frame decoder with a memory-bounded cache.
// Frames are decoded in "look-ahead order" starting at the playhead in the
// play direction; when the budget is exhausted, frames farthest away in that
// order are evicted first. If the whole range fits, playback is fully cached.
class FrameCache {
public:
    FrameCache();
    ~FrameCache();

    void setSequence(std::shared_ptr<const Sequence> seq);
    void setBudget(size_t bytes);
    // Changes what is decoded (EXR layer, Cryptomatte); drops cached frames.
    void setLoadOptions(LoadOptionsPtr opts);
    void setPlayhead(int index, int direction, int rangeStart, int rangeEnd, bool wrap);
    void clear();

    ImagePtr get(int index) const;
    // 0 = not cached, 1 = cached, 2 = error
    void stateMask(std::vector<uint8_t>& out) const;
    size_t usedBytes() const;

    // Called from worker threads after each decoded frame.
    void setOnFrameReady(std::function<void()> fn);

private:
    void workerLoop();
    bool pickWork(int& index, uint64_t& gen);
    size_t distance(int index) const;

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::vector<std::thread> m_threads;
    bool m_quit = false;

    std::function<void()> m_onFrameReady;
    LoadOptionsPtr m_opts;
    std::shared_ptr<const Sequence> m_seq;
    uint64_t m_generation = 0;
    std::unordered_map<int, ImagePtr> m_frames;
    std::set<int> m_inflight;
    size_t m_used = 0;
    size_t m_budget = size_t(4) << 30;
    size_t m_frameEstimate = 0;

    int m_playhead = 0, m_direction = 1, m_rangeStart = 0, m_rangeEnd = 0;
    bool m_wrap = true;
};
