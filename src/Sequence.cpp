#include "Sequence.h"
#include "ImageIO.h"
#include "Platform.h"

#include <windows.h>
#include <algorithm>
#include <cwctype>
#include <map>

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

int Sequence::indexOfNumber(int number) const
{
    if (frames.empty()) return 0;
    auto it = std::lower_bound(frames.begin(), frames.end(), number, [](const SequenceFrame& f, int n) { return f.number < n; });
    if (it == frames.end()) return count() - 1;
    if (it != frames.begin() && number - std::prev(it)->number < it->number - number) --it;
    return int(it - frames.begin());
}

static uint64_t Join(DWORD high, DWORD low) { return (uint64_t(high) << 32) | low; }

// Files (or folders, with dirs) of `dir` matching the pattern. The pattern only narrows the
// listing: callers check every name themselves.
template <class F>
static void EnumerateDir(const std::wstring& dir, const std::wstring& pattern, F&& fn, bool dirs = false)
{
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW((dir + L"\\" + pattern).c_str(), FindExInfoBasic, &fd,
                                FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) {
        // Windows hands "a.*.exr" to the file system as DOS wildcards (a"<.exr), which some
        // virtual drives (cloud sync, FUSE-style mounts) do not match: list everything instead.
        if (pattern != L"*" && GetLastError() == ERROR_FILE_NOT_FOUND) EnumerateDir(dir, L"*", fn, dirs);
        return;
    }
    do {
        const bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (isDir == dirs && fd.cFileName[0] != L'.') fn(fd);
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

static bool AllDigits(const std::wstring& s)
{
    return !s.empty() && s.size() <= 9 && std::all_of(s.begin(), s.end(), [](wchar_t c) { return std::iswdigit(c); });
}

// Collects files named prefix<digits>suffix. Padded numbers ("0042") must keep
// their width; unpadded ones ("42", "1001") may vary.
static std::vector<SequenceFrame> CollectFrames(const std::wstring& dir, const std::wstring& prefix,
                                                const std::wstring& digits, const std::wstring& suffix)
{
    std::vector<SequenceFrame> frames;
    const bool padded = digits.size() > 1 && digits[0] == L'0';
    const std::wstring lprefix = ToLower(prefix), lsuffix = ToLower(suffix);
    EnumerateDir(dir, prefix + L"*" + suffix, [&](const WIN32_FIND_DATAW& fd) {
        const std::wstring f = fd.cFileName;
        if (f.size() <= prefix.size() + suffix.size()) return;
        const std::wstring lf = ToLower(f);
        if (lf.compare(0, lprefix.size(), lprefix) != 0) return;
        if (lf.compare(lf.size() - lsuffix.size(), lsuffix.size(), lsuffix) != 0) return;
        const std::wstring mid = f.substr(prefix.size(), f.size() - prefix.size() - suffix.size());
        if (!AllDigits(mid)) return;
        if (padded && mid.size() != digits.size()) return;
        frames.push_back({ std::stoi(mid), dir + L"\\" + f, Join(fd.ftLastWriteTime.dwHighDateTime, fd.ftLastWriteTime.dwLowDateTime),
                           Join(fd.nFileSizeHigh, fd.nFileSizeLow) });
    });
    std::sort(frames.begin(), frames.end(), [](auto& a, auto& b) { return a.number < b.number; });
    return frames;
}

static SequenceFrame SingleFrame(int number, const std::wstring& path)
{
    SequenceFrame f{ number, path };
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a)) {
        f.stamp = Join(a.ftLastWriteTime.dwHighDateTime, a.ftLastWriteTime.dwLowDateTime);
        f.size = Join(a.nFileSizeHigh, a.nFileSizeLow);
    }
    return f;
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
                seq.padded = l0 > 1 && name[s0] == L'0';
                seq.frames = { SingleFrame(std::stoi(name.substr(s0, l0)), path) };
                return seq;
            }
            seq.prefix = prefix;
            seq.suffix = suffix;
            seq.padding = (int)len;
            seq.padded = len > 1 && digits[0] == L'0';
            seq.frames = frames.empty() ? std::vector<SequenceFrame>{ SingleFrame(std::stoi(digits), path) } : std::move(frames);
            if (startIndex) *startIndex = seq.indexOfPath(path);
            return seq;
        }
    }

    // No digits at all: single image.
    seq.frames.push_back(SingleFrame(0, path));
    seq.suffix = GetFileExtension(path);
    return seq;
}

Sequence DetectSequence(const std::wstring& path, int* startIndex)
{
    if (startIndex) *startIndex = 0;
    if (!IsDirectory(path)) return DetectFromFile(path, startIndex);

    // Folder: pick alphabetically first supported image, then its sequence.
    std::wstring first;
    EnumerateDir(path, L"*", [&](const WIN32_FIND_DATAW& fd) {
        const std::wstring f = fd.cFileName;
        if (IsSupportedExtension(GetFileExtension(f)) && (first.empty() || ToLower(f) < ToLower(first)))
            first = f;
    });
    if (first.empty()) return {};
    Sequence seq = DetectFromFile(path + L"\\" + first, nullptr);
    return seq;
}

// Digits accepted by CollectFrames for the frame numbers of `seq`.
static std::wstring DigitsTemplate(const Sequence& seq)
{
    return seq.padded ? std::wstring(seq.padding, L'0') : std::wstring(L"1");
}

Sequence RescanSequence(const Sequence& seq)
{
    Sequence out = seq;
    if (seq.frames.empty()) return out;
    if (seq.padding == 0) {   // single image without a number
        out.frames = { SingleFrame(seq.frames[0].number, seq.frames[0].path) };
        if (!FileExists(seq.frames[0].path)) out.frames.clear();
        return out;
    }
    out.frames = CollectFrames(seq.directory, seq.prefix, DigitsTemplate(seq), seq.suffix);
    return out;
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

// ---------------------------------------------------------------------------
// Versions

// "v<digits>" tokens of `s` as (start, length): the v not preceded by a letter or digit,
// the digits not followed by a letter.
static std::vector<std::pair<size_t, size_t>> VersionTokens(const std::wstring& s)
{
    std::vector<std::pair<size_t, size_t>> out;
    for (size_t i = 0; i < s.size(); ++i) {
        if ((s[i] != L'v' && s[i] != L'V') || (i > 0 && std::iswalnum(s[i - 1]))) continue;
        size_t j = i + 1;
        while (j < s.size() && std::iswdigit(s[j])) ++j;
        if (j == i + 1 || j - i - 1 > 6 || (j < s.size() && std::iswalpha(s[j]))) continue;
        out.push_back({ i, j - i });
        i = j - 1;
    }
    return out;
}

// The path a version is looked up in: the last two folders and the name before the frame number.
static std::wstring VersionedPath(const Sequence& seq, size_t& searchFrom)
{
    const std::wstring full = seq.directory + L"\\" + seq.prefix;
    size_t cut = full.find_last_of(L'\\');
    for (int k = 0; k < 2 && cut != std::wstring::npos && cut > 0; ++k) cut = full.find_last_of(L'\\', cut - 1);
    searchFrom = cut == std::wstring::npos ? 0 : cut;
    return full;
}

static std::vector<std::pair<size_t, size_t>> SearchedTokens(const Sequence& seq, std::wstring& full)
{
    if (seq.frames.empty() || seq.padding == 0) return {};
    size_t from = 0;
    full = VersionedPath(seq, from);
    auto tokens = VersionTokens(full);
    tokens.erase(std::remove_if(tokens.begin(), tokens.end(), [&](auto& t) { return t.first < from; }), tokens.end());
    return tokens;
}

std::wstring VersionToken(const Sequence& seq)
{
    std::wstring full;
    const auto tokens = SearchedTokens(seq, full);
    return tokens.empty() ? std::wstring() : full.substr(tokens.back().first, tokens.back().second);
}

std::wstring ReplaceVersion(const std::wstring& path, const std::wstring& from, const std::wstring& to)
{
    std::wstring out = path;
    const std::wstring lfrom = ToLower(from);
    const auto tokens = VersionTokens(path);
    for (auto it = tokens.rbegin(); it != tokens.rend(); ++it)
        if (ToLower(path.substr(it->first, it->second)) == lfrom) out.replace(it->first, it->second, to);
    return out;
}

std::vector<SequenceVersion> FindVersions(const Sequence& seq)
{
    std::vector<SequenceVersion> out;
    std::wstring full;
    const auto tokens = SearchedTokens(seq, full);
    if (tokens.empty()) return out;
    const std::wstring cur = full.substr(tokens.back().first, tokens.back().second);
    const int curNumber = std::stoi(cur.substr(1));
    // Every occurrence of the token changes together ("v002\shot_v002.").
    std::vector<std::pair<size_t, size_t>> occ;
    for (const auto& t : tokens)
        if (ToLower(full.substr(t.first, t.second)) == ToLower(cur)) occ.push_back(t);

    // The highest path component with the token: its siblings are the candidates.
    const size_t p0 = occ.front().first;
    const size_t compStart = full.find_last_of(L'\\', p0) + 1;
    const size_t compEnd = full.find(L'\\', p0);
    const bool isFile = compEnd == std::wstring::npos;
    const std::wstring parent = full.substr(0, compStart - 1);
    const std::wstring comp = full.substr(compStart, isFile ? std::wstring::npos : compEnd - compStart);
    std::vector<std::wstring> pieces;   // literal text around the tokens of the component
    size_t last = 0;
    for (const auto& t : occ) {
        if (!isFile && t.first >= compEnd) break;
        pieces.push_back(comp.substr(last, t.first - compStart - last));
        last = t.first - compStart + t.second;
    }
    pieces.push_back(comp.substr(last));

    // name = pieces[0] v<n> pieces[1] ... v<n> pieces[k] rest
    auto parse = [&](const std::wstring& name, std::wstring& token, int& number, std::wstring& rest) {
        const std::wstring lname = ToLower(name);
        size_t pos = 0;
        number = -1;
        for (size_t k = 0; k < pieces.size(); ++k) {
            const std::wstring lp = ToLower(pieces[k]);
            if (lname.compare(pos, lp.size(), lp) != 0) return false;
            pos += lp.size();
            if (k + 1 == pieces.size()) break;
            if (pos >= name.size() || (name[pos] != L'v' && name[pos] != L'V')) return false;
            size_t j = pos + 1;
            while (j < name.size() && std::iswdigit(name[j])) ++j;
            if (j == pos + 1 || j - pos - 1 > 6) return false;
            const int n = std::stoi(name.substr(pos + 1, j - pos - 1));
            if (number >= 0 && n != number) return false;
            number = n;
            token = name.substr(pos, j - pos);
            pos = j;
        }
        rest = name.substr(pos);
        return number >= 0;
    };

    std::map<int, std::wstring> found;   // number -> token as written
    const std::wstring lsuffix = ToLower(seq.suffix);
    EnumerateDir(parent, pieces[0] + L"*", [&](const WIN32_FIND_DATAW& fd) {
        std::wstring token, rest;
        int number = 0;
        if (!parse(fd.cFileName, token, number, rest) || found.count(number)) return;
        if (isFile) {   // rest = frame number + suffix
            const std::wstring lrest = ToLower(rest);
            if (lrest.size() <= lsuffix.size() || lrest.compare(lrest.size() - lsuffix.size(), lsuffix.size(), lsuffix) != 0) return;
            if (!AllDigits(rest.substr(0, rest.size() - lsuffix.size()))) return;
        } else if (!rest.empty()) {
            return;
        }
        found[number] = token;
    }, !isFile);

    for (const auto& [number, token] : found) {
        std::wstring path = full;
        for (auto it = occ.rbegin(); it != occ.rend(); ++it) path.replace(it->first, it->second, token);
        const size_t slash = path.find_last_of(L'\\');
        const auto frames = CollectFrames(path.substr(0, slash), path.substr(slash + 1), DigitsTemplate(seq), seq.suffix);
        if (frames.empty()) continue;
        out.push_back({ token, number, frames[0].path, (int)frames.size(), number == curNumber });
    }
    return out;
}
