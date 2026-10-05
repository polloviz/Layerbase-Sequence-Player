#include "FrameCache.h"
#include "ImageIO.h"
#include "Platform.h"

#include <algorithm>
#include <array>
#include <windows.h>

bool FrameSet::complete() const
{
    return std::all_of(images.begin(), images.end(), [](const ImagePtr& i) { return i != nullptr; });
}

ImagePtr FrameSet::find(const std::string& key) const
{
    for (size_t i = 0; i < images.size() && plan && i < plan->layers.size(); ++i)
        if (plan->layers[i].key == key) return images[i];
    return nullptr;
}

namespace {

size_t SetBytes(const std::vector<ImagePtr>& images)
{
    size_t n = 0;
    for (auto& i : images)
        if (i) n += i->memorySize();
    return n;
}

bool AllValid(const std::vector<ImagePtr>& images)
{
    return std::all_of(images.begin(), images.end(), [](const ImagePtr& i) { return i && i->valid(); });
}

ImagePtr MissingFrame()
{
    static const ImagePtr img = [] {
        auto i = std::make_shared<Image>();
        i->error = "Missing frame";
        return i;
    }();
    return img;
}

}  // namespace

// Layers read from the same EXR file are decoded together, so the file is opened and
// decompressed once.
void DecodeLayers(const Sequence& seq, int index, const LoadPlan& plan, std::vector<ImagePtr>& images)
{
    std::vector<std::pair<std::wstring, std::vector<size_t>>> byFile;   // first-use order
    for (size_t k = 0; k < plan.layers.size(); ++k) {
        if (images[k]) continue;
        const LayerLoad& l = plan.layers[k];
        const std::wstring path = !l.files ? seq.frames[index].path
                                : index < (int)l.files->size() ? (*l.files)[index] : std::wstring();
        if (path.empty()) {
            images[k] = MissingFrame();
            continue;
        }
        auto it = std::find_if(byFile.begin(), byFile.end(), [&](auto& f) { return f.first == path; });
        if (it == byFile.end()) byFile.push_back({ path, { k } });
        else it->second.push_back(k);
    }
    static const LoadOptions kDefault;
    // Denoiser guides of each layer (albedo, normal, motion vectors): EXR layers of the same file.
    constexpr int kGuides = 3;
    std::vector<std::array<ImagePtr, kGuides>> guides(plan.layers.size());
    auto guideOpts = [&](size_t k, int g) -> const LoadOptions* {
        const DenoiseSpecPtr& d = plan.layers[k].denoise;
        return !d ? nullptr : g == 0 ? d->albedo.get() : g == 1 ? d->normal.get() : d->temporal ? d->flow.get() : nullptr;
    };
    // Temporal denoise chains frames per stream; the frames being decoded are announced first,
    // so the next frame waits for this one instead of starting a new chain.
    std::vector<std::string> streams(plan.layers.size());
    for (const auto& [path, layers] : byFile)
        for (size_t k : layers)
            if (plan.layers[k].denoise && plan.layers[k].denoise->temporal) {
                streams[k] = ToUtf8(seq.directory + L"\\" + seq.displayName()) + "|" + plan.layers[k].key + "|" + std::to_string(plan.proxy);
                DenoiseFrameStarted(streams[k], index);
            }
    struct Finish {
        const std::vector<std::string>& streams;
        int index;
        ~Finish() {
            for (const auto& s : streams)
                if (!s.empty()) DenoiseFrameFinished(s, index);
        }
    } finish{ streams, index };

    for (const auto& [path, layers] : byFile) {
        const bool exr = GetFileExtension(path) == L".exr";
        size_t reads = layers.size();
        for (size_t k : layers)
            for (int g = 0; g < kGuides; ++g) reads += exr && guideOpts(k, g);
        bool together = reads > 1 && exr;
        for (size_t k : layers) together = together && !(plan.layers[k].opts && plan.layers[k].opts->cryptoActive());
        if (together) {
            std::vector<const LoadOptions*> opts;
            std::vector<std::pair<size_t, int>> dest;   // (layer, -1 = the layer itself / guide index)
            for (size_t k : layers) {
                opts.push_back(plan.layers[k].opts ? plan.layers[k].opts.get() : &kDefault);
                dest.push_back({ k, -1 });
                for (int g = 0; g < kGuides; ++g)
                    if (const LoadOptions* o = guideOpts(k, g)) { opts.push_back(o); dest.push_back({ k, g }); }
            }
            std::vector<ImagePtr> imgs = LoadExrLayers(path, opts);
            for (size_t j = 0; j < dest.size(); ++j)
                (dest[j].second < 0 ? images[dest[j].first] : guides[dest[j].first][dest[j].second]) = imgs[j];
            continue;
        }
        for (size_t k : layers) {
            const LoadOptions* o = plan.layers[k].opts.get();
            std::wstring matte;
            if (o && o->cryptoFiles && index < (int)o->cryptoFiles->size()) matte = (*o->cryptoFiles)[index];
            images[k] = LoadImageFile(path, o, matte);
            std::vector<const LoadOptions*> opts;
            std::vector<int> which;
            for (int g = 0; g < kGuides; ++g)
                if (exr && guideOpts(k, g)) { opts.push_back(guideOpts(k, g)); which.push_back(g); }
            if (!opts.empty()) {
                std::vector<ImagePtr> imgs = LoadExrLayers(path, opts);
                for (size_t j = 0; j < which.size(); ++j) guides[k][which[j]] = imgs[j];
            }
        }
    }
    if (plan.proxy > 1)
        for (const auto& [path, layers] : byFile)
            for (size_t k : layers) {
                images[k] = Downscale(images[k], plan.proxy);
                for (ImagePtr& g : guides[k]) g = Downscale(g, plan.proxy);
            }
    // Filters run last, on the frame as it is shown (a proxy frame denoises much faster).
    for (const auto& [path, layers] : byFile)
        for (size_t k : layers)
            if (plan.layers[k].denoise && images[k] && images[k]->valid()) {
                DenoiseInput in;
                in.color = images[k];
                in.albedo = guides[k][0];
                in.normal = guides[k][1];
                in.flow = guides[k][2];
                in.flowScale = 1.0f / float(std::max(1, plan.proxy));
                in.stream = streams[k];
                in.frame = streams[k].empty() ? -1 : index;
                images[k] = DenoiseImage(in, *plan.layers[k].denoise);
            }
}

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

void FrameCache::remap(std::shared_ptr<const Sequence> seq, const std::vector<int>& from)
{
    {
        std::lock_guard lock(m_mutex);
        m_seq = std::move(seq);
        ++m_generation;
        m_inflight.clear();
        std::unordered_map<int, FrameSetPtr> frames;
        m_used = 0;
        for (auto& [i, set] : m_frames) {
            const int to = i >= 0 && i < (int)from.size() ? from[i] : -1;
            if (to < 0 || !set->complete() || !AllValid(set->images)) continue;
            m_used += set->bytes;
            frames[to] = std::move(set);
        }
        m_frames = std::move(frames);
        m_rangeEnd = std::min(m_rangeEnd, m_seq ? std::max(0, m_seq->count() - 1) : 0);
        m_rangeStart = std::min(m_rangeStart, m_rangeEnd);
        m_playhead = std::clamp(m_playhead, m_rangeStart, m_rangeEnd);
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

void FrameCache::setPlan(LoadPlanPtr plan)
{
    {
        std::lock_guard lock(m_mutex);
        const LoadPlanPtr old = std::move(m_plan);
        m_plan = std::move(plan);
        ++m_generation;
        m_inflight.clear();
        m_frameEstimate = 0;

        // New layer index -> old layer index with the same key (and the same resolution).
        const size_t n = m_plan ? m_plan->layers.size() : 0;
        std::vector<int> from(n, -1);
        bool keep = false;
        for (size_t k = 0; k < n && old && old->proxy == m_plan->proxy; ++k)
            for (size_t j = 0; j < old->layers.size(); ++j)
                if (old->layers[j].key == m_plan->layers[k].key) { from[k] = (int)j; keep = true; break; }

        m_used = 0;
        if (!keep) {
            m_frames.clear();
        } else {
            for (auto it = m_frames.begin(); it != m_frames.end();) {
                auto set = std::make_shared<FrameSet>();
                set->plan = m_plan;
                set->images.resize(n);
                bool any = false;
                for (size_t k = 0; k < n; ++k)
                    if (from[k] >= 0 && (set->images[k] = it->second->images[from[k]])) any = true;
                if (!any) { it = m_frames.erase(it); continue; }
                set->bytes = SetBytes(set->images);
                if (AllValid(set->images)) m_frameEstimate = std::max(m_frameEstimate, set->bytes);
                m_used += set->bytes;
                it->second = std::move(set);
                ++it;
            }
        }
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

FrameSetPtr FrameCache::get(int index) const
{
    std::lock_guard lock(m_mutex);
    auto it = m_frames.find(index);
    return it == m_frames.end() || !it->second->complete() ? nullptr : it->second;
}

void FrameCache::stateMask(std::vector<uint8_t>& out) const
{
    std::lock_guard lock(m_mutex);
    out.assign(m_seq ? m_seq->count() : 0, 0);
    for (auto& [i, set] : m_frames)
        if (i >= 0 && i < (int)out.size() && set->complete()) out[i] = AllValid(set->images) ? 1 : 2;
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
    if (!m_seq || m_seq->empty() || !m_plan) return false;
    const int len = m_rangeEnd - m_rangeStart + 1;
    const size_t need = m_frameEstimate;

    for (int k = 0; k < len; ++k) {
        int i = m_playhead + k * m_direction;
        if (i > m_rangeEnd || i < m_rangeStart) {
            if (!m_wrap) break;
            i = m_rangeStart + ((i - m_rangeStart) % len + len) % len;
        }
        if (m_inflight.count(i)) continue;
        auto cached = m_frames.find(i);
        if (cached != m_frames.end() && cached->second->complete()) continue;

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
            m_used -= victim->second->bytes;
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
        const std::shared_ptr<const Sequence> seq = m_seq;
        const LoadPlanPtr plan = m_plan;
        std::vector<ImagePtr> images(plan->layers.size());
        if (auto it = m_frames.find(index); it != m_frames.end()) images = it->second->images;   // decode only what is missing
        lock.unlock();

        DecodeLayers(*seq, index, *plan, images);

        lock.lock();
        if (gen != m_generation) continue;   // sequence or plan changed meanwhile
        m_inflight.erase(index);
        auto set = std::make_shared<FrameSet>();
        set->plan = plan;
        set->bytes = SetBytes(images);
        set->images = std::move(images);
        FrameSetPtr& slot = m_frames[index];
        if (slot) m_used -= slot->bytes;
        m_used += set->bytes;
        if (AllValid(set->images)) m_frameEstimate = std::max(m_frameEstimate, set->bytes);
        slot = std::move(set);
        auto cb = m_onFrameReady;
        lock.unlock();
        if (cb) cb();
        lock.lock();
    }
}
