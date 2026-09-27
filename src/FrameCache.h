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

// One layer decoded for every frame: which file and which EXR channels.
struct LayerLoad {
    std::string key;   // identity of the decoded pixels: layers with the same key are interchangeable
    // File per frame of the sequence ("" = none for that frame); null = the sequence's own frames.
    std::shared_ptr<const std::vector<std::wstring>> files;
    LoadOptionsPtr opts;
};
struct LoadPlan {
    std::vector<LayerLoad> layers;
    int proxy = 1;     // decode at 1/proxy of the file resolution (playback proxy)
};
using LoadPlanPtr = std::shared_ptr<const LoadPlan>;

// The decoded layers of one frame, aligned with plan->layers (null = not decoded yet).
struct FrameSet {
    LoadPlanPtr plan;
    std::vector<ImagePtr> images;
    size_t bytes = 0;

    bool complete() const;
    ImagePtr find(const std::string& key) const;
};
using FrameSetPtr = std::shared_ptr<const FrameSet>;

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
    // Changes what is decoded. Layers whose key is still planned are kept, so adding a
    // layer decodes only that layer.
    void setPlan(LoadPlanPtr plan);
    void setPlayhead(int index, int direction, int rangeStart, int rangeEnd, bool wrap);
    void clear();

    FrameSetPtr get(int index) const;   // frames with every layer decoded, else null
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
    LoadPlanPtr m_plan;
    std::shared_ptr<const Sequence> m_seq;
    uint64_t m_generation = 0;
    std::unordered_map<int, FrameSetPtr> m_frames;
    std::set<int> m_inflight;
    size_t m_used = 0;
    size_t m_budget = size_t(4) << 30;
    size_t m_frameEstimate = 0;

    int m_playhead = 0, m_direction = 1, m_rangeStart = 0, m_rangeEnd = 0;
    bool m_wrap = true;
};
