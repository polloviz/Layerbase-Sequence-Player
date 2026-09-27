#include "Sequence.h"
#include "ImageIO.h"
#include "Platform.h"

#include <windows.h>
#include <algorithm>
#include <cwctype>

std::wstring Sequence::displayName() const
{
    if (frames.empty()) return {};
    if (padding == 0 && frames.size() == 1) return GetFileName(frames[0].path);
    return prefix + std::wstring(std::max(1, padding), L'#') + suffix;
}

int Sequence::indexOfPath(const std::wstring& path) const
{
    const std::wstring lp = ToLower(path);
    for (size_t i = 0; i < frames.size(); ++i)
        if (ToLower(frames[i].path) == lp) return (int)i;
    return 0;
}

template <class F>
static void EnumerateDir(const std::wstring& dir, const std::wstring& pattern, F&& fn)
{
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW((dir + L"\\" + pattern).c_str(), FindExInfoBasic, &fd,
                                FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) fn(std::wstring(fd.cFileName));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

// Every run of digits in the file stem, as (start, length), last one first.
static std::vector<std::pair<size_t, size_t>> DigitGroups(const std::wstring& file)
{
    std::vector<std::pair<size_t, size_t>> groups;
    size_t end = file.find_last_of(L'.');
    if (end == std::wstring::npos) end = file.size();
    while (end > 0) {
        while (end > 0 && !std::iswdigit(file[end - 1])) --end;
        if (end == 0) break;
        size_t start = end;
        while (start > 0 && std::iswdigit(file[start - 1])) --start;
        if (end - start <= 9) groups.push_back({ start, end - start });
        end = start;
    }
    return groups;
}

// Collects files named prefix<digits>suffix. Padded numbers ("0042") must keep
// their width; unpadded ones ("42", "1001") may vary.
static std::vector<SequenceFrame> CollectFrames(const std::wstring& dir, const std::wstring& prefix,
                                                const std::wstring& digits, const std::wstring& suffix)
{
    std::vector<SequenceFrame> frames;
    const bool padded = digits.size() > 1 && digits[0] == L'0';
    const std::wstring lprefix = ToLower(prefix), lsuffix = ToLower(suffix);
    EnumerateDir(dir, prefix + L"*" + suffix, [&](const std::wstring& f) {
        if (f.size() <= prefix.size() + suffix.size()) return;
        const std::wstring lf = ToLower(f);
        if (lf.compare(0, lprefix.size(), lprefix) != 0) return;
        if (lf.compare(lf.size() - lsuffix.size(), lsuffix.size(), lsuffix) != 0) return;
        const std::wstring mid = f.substr(prefix.size(), f.size() - prefix.size() - suffix.size());
        if (mid.empty() || mid.size() > 9 || !std::all_of(mid.begin(), mid.end(), [](wchar_t c) { return std::iswdigit(c); }))
            return;
        if (padded && mid.size() != digits.size()) return;
        frames.push_back({ std::stoi(mid), dir + L"\\" + f });
    });
    std::sort(frames.begin(), frames.end(), [](auto& a, auto& b) { return a.number < b.number; });
    return frames;
}

static Sequence DetectFromFile(const std::wstring& path, int* startIndex)
{
    Sequence seq;
    seq.directory = GetParentDir(path);
    const std::wstring name = GetFileName(path);
    if (startIndex) *startIndex = 0;

    // The frame number is usually the last digit group ("shot_v02.1001.exr"),
    // but not always ("render_1001_v02.exr"): use the last group that yields
    // a real sequence.
    const auto groups = DigitGroups(name);
    for (size_t g = 0; g < groups.size(); ++g) {
        const auto [start, len] = groups[g];
        const std::wstring prefix = name.substr(0, start), digits = name.substr(start, len), suffix = name.substr(start + len);
        auto frames = CollectFrames(seq.directory, prefix, digits, suffix);
        if (frames.size() > 1 || g + 1 == groups.size()) {
            if (frames.size() <= 1 && g > 0) {
                // No group formed a sequence: keep the conventional last group.
                const auto [s0, l0] = groups[0];
                seq.prefix = name.substr(0, s0);
                seq.suffix = name.substr(s0 + l0);
                seq.padding = (int)l0;
                seq.frames = { { std::stoi(name.substr(s0, l0)), path } };
                return seq;
            }
            seq.prefix = prefix;
            seq.suffix = suffix;
            seq.padding = (int)len;
            seq.frames = frames.empty() ? std::vector<SequenceFrame>{ { std::stoi(digits), path } } : std::move(frames);
            if (startIndex) *startIndex = seq.indexOfPath(path);
            return seq;
        }
    }

    // No digits at all: single image.
    seq.frames.push_back({ 0, path });
    seq.suffix = GetFileExtension(path);
    return seq;
}

Sequence DetectSequence(const std::wstring& path, int* startIndex)
{
    if (startIndex) *startIndex = 0;
    if (!IsDirectory(path)) return DetectFromFile(path, startIndex);

    // Folder: pick alphabetically first supported image, then its sequence.
    std::wstring first;
    EnumerateDir(path, L"*", [&](const std::wstring& f) {
        if (IsSupportedExtension(GetFileExtension(f)) && (first.empty() || ToLower(f) < ToLower(first)))
            first = f;
    });
    if (first.empty()) return {};
    Sequence seq = DetectFromFile(path + L"\\" + first, nullptr);
    return seq;
}

std::vector<Sequence> FindSequences(const std::wstring& root, bool recursive)
{
    std::vector<Sequence> out;
    std::vector<std::wstring> dirs{ root };
    for (size_t d = 0; d < dirs.size(); ++d) {
        const std::wstring dir = dirs[d];
        std::vector<std::wstring> files;
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileExW((dir + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            const std::wstring name = fd.cFileName;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                // Skip "." / ".." and junctions (avoid cycles).
                if (recursive && name != L"." && name != L".." && !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                    dirs.push_back(dir + L"\\" + name);
            } else if (IsSupportedExtension(GetFileExtension(name))) {
                files.push_back(name);
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);

        std::sort(files.begin(), files.end(), [](auto& a, auto& b) { return ToLower(a) < ToLower(b); });
        std::vector<std::wstring> used;   // lowercase names already part of a sequence (sorted)
        for (const auto& f : files) {
            const std::wstring lf = ToLower(f);
            if (std::binary_search(used.begin(), used.end(), lf)) continue;
            Sequence s = DetectFromFile(dir + L"\\" + f, nullptr);
            for (const auto& fr : s.frames) used.push_back(ToLower(GetFileName(fr.path)));
            std::sort(used.begin(), used.end());
            if (s.count() >= 2) out.push_back(std::move(s));
        }
    }
    return out;
}

std::wstring SequenceBaseName(const Sequence& seq)
{
    std::wstring base = seq.prefix;
    while (!base.empty() && (base.back() == L'.' || base.back() == L'_' || base.back() == L'-' || base.back() == L' ')) base.pop_back();
    if (base.empty() && !seq.frames.empty()) {
        base = GetFileName(seq.frames[0].path);
        const size_t dot = base.find_last_of(L'.');
        if (dot != std::wstring::npos) base = base.substr(0, dot);
    }
    return base.empty() ? L"export" : base;
}

std::vector<std::wstring> MatchFrames(const Sequence& seq, const Sequence& other)
{
    std::vector<std::wstring> files(seq.frames.size());
    bool hit = false;
    for (size_t i = 0; i < seq.frames.size(); ++i) {
        const int number = seq.frames[i].number;
        auto it = std::lower_bound(other.frames.begin(), other.frames.end(), number,
                                   [](const SequenceFrame& f, int n) { return f.number < n; });
        if (it != other.frames.end() && it->number == number) { files[i] = it->path; hit = true; }
    }
    if (!hit)
        for (size_t i = 0; i < seq.frames.size() && i < other.frames.size(); ++i) files[i] = other.frames[i].path;
    return files;
}
