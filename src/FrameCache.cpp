#include "FrameCache.h"
#include "ImageIO.h"

#include <algorithm>
#include <windows.h>

FrameCache::FrameCache()
{
    const unsigned hw = std::max(2u, std::thread::hardware_concurrency());
    const unsigned workers = std::clamp(hw / 2, 2u, 8u);
    for (unsigned i = 0; i < workers; ++i)
        m_threads.emplace_back([this] {
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
            workerLoop();
        });
}

FrameCache::~FrameCache()
{
    {
        std::lock_guard lock(m_mutex);
        m_quit = true;
    }
    m_cv.notify_all();
    for (auto& t : m_threads) t.join();
}

void FrameCache::setSequence(std::shared_ptr<const Sequence> seq)
{
    {
        std::lock_guard lock(m_mutex);
        m_seq = std::move(seq);
        ++m_generation;
        m_frames.clear();
        m_inflight.clear();
        m_used = 0;
        m_frameEstimate = 0;
        m_playhead = 0;
        m_direction = 1;
        m_rangeStart = 0;
        m_rangeEnd = m_seq ? std::max(0, m_seq->count() - 1) : 0;
    }
    m_cv.notify_all();
}

void FrameCache::setOnFrameReady(std::function<void()> fn)
{
    std::lock_guard lock(m_mutex);
    m_onFrameReady = std::move(fn);
}

void FrameCache::clear()
{
    std::lock_guard lock(m_mutex);
    ++m_generation;
    m_frames.clear();
    m_inflight.clear();
    m_used = 0;
    m_cv.notify_all();
}

void FrameCache::setLoadOptions(LoadOptionsPtr opts)
{
    {
        std::lock_guard lock(m_mutex);
        m_opts = std::move(opts);
        ++m_generation;
        m_frames.clear();
        m_inflight.clear();
        m_used = 0;
    }
    m_cv.notify_all();
}

void FrameCache::setBudget(size_t bytes)
{
    {
        std::lock_guard lock(m_mutex);
        m_budget = std::max<size_t>(bytes, 64u << 20);
    }
    m_cv.notify_all();
}

void FrameCache::setPlayhead(int index, int direction, int rangeStart, int rangeEnd, bool wrap)
{
    {
        std::lock_guard lock(m_mutex);
        if (m_playhead == index && m_direction == direction && m_rangeStart == rangeStart &&
            m_rangeEnd == rangeEnd && m_wrap == wrap)
            return;
        m_playhead = index;
        m_direction = direction >= 0 ? 1 : -1;
        m_rangeStart = rangeStart;
        m_rangeEnd = rangeEnd;
        m_wrap = wrap;
    }
    m_cv.notify_all();
}

ImagePtr FrameCache::get(int index) const
{
    std::lock_guard lock(m_mutex);
    auto it = m_frames.find(index);
    return it == m_frames.end() ? nullptr : it->second;
}

void FrameCache::stateMask(std::vector<uint8_t>& out) const
{
    std::lock_guard lock(m_mutex);
    out.assign(m_seq ? m_seq->count() : 0, 0);
    for (auto& [i, img] : m_frames)
        if (i >= 0 && i < (int)out.size()) out[i] = img->valid() ? 1 : 2;
}

size_t FrameCache::usedBytes() const
{
    std::lock_guard lock(m_mutex);
    return m_used;
}

// Position of a frame in look-ahead order (0 = playhead). Out of range = max.
size_t FrameCache::distance(int index) const
{
    if (index < m_rangeStart || index > m_rangeEnd) return SIZE_MAX;
    const int len = m_rangeEnd - m_rangeStart + 1;
    int d = (index - m_playhead) * m_direction;
    if (d < 0) {
        if (!m_wrap) return size_t(len) + size_t(-d);   // behind: evict first
        d += len;
    }
    return size_t(d);
}

bool FrameCache::pickWork(int& index, uint64_t& gen)
{
    if (!m_seq || m_seq->empty()) return false;
    const int len = m_rangeEnd - m_rangeStart + 1;
    const size_t need = m_frameEstimate;

    for (int k = 0; k < len; ++k) {
        int i = m_playhead + k * m_direction;
        if (i > m_rangeEnd || i < m_rangeStart) {
            if (!m_wrap) break;
            i = m_rangeStart + ((i - m_rangeStart) % len + len) % len;
        }
        if (m_frames.count(i) || m_inflight.count(i)) continue;

        // Make room: evict frames that are farther away than this one.
        const size_t reserved = need * m_inflight.size();
        while (m_used + reserved + need > m_budget) {
            const size_t myDist = distance(i);
            auto victim = m_frames.end();
            size_t worst = myDist;
            for (auto it = m_frames.begin(); it != m_frames.end(); ++it) {
                const size_t d = distance(it->first);
                if (d > worst) { worst = d; victim = it; }
            }
            if (victim == m_frames.end()) return false;   // cache full of closer frames
            m_used -= victim->second->memorySize();
            m_frames.erase(victim);
        }
        index = i;
        gen = m_generation;
        m_inflight.insert(i);
        return true;
    }
    return false;
}

void FrameCache::workerLoop()
{
    std::unique_lock lock(m_mutex);
    while (!m_quit) {
        int index = -1;
        uint64_t gen = 0;
        if (!pickWork(index, gen)) {
            m_cv.wait(lock);
            continue;
        }
        const std::wstring path = m_seq->frames[index].path;
        const LoadOptionsPtr opts = m_opts;
        lock.unlock();

        std::wstring matte;
        if (opts && opts->cryptoFiles && index < (int)opts->cryptoFiles->size()) matte = (*opts->cryptoFiles)[index];
        ImagePtr img = LoadImageFile(path, opts.get(), matte);

        lock.lock();
        if (gen != m_generation) continue;   // sequence changed meanwhile
        m_inflight.erase(index);
        m_frames[index] = img;
        m_used += img->memorySize();
        if (img->valid()) m_frameEstimate = std::max(m_frameEstimate, img->memorySize());
        auto cb = m_onFrameReady;
        lock.unlock();
        if (cb) cb();
        lock.lock();
    }
}
