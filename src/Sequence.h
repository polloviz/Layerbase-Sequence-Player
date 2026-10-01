#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct SequenceFrame {
    int number = 0;
    std::wstring path;
    uint64_t stamp = 0;      // last write time (FILETIME); 0 = unknown
    uint64_t size = 0;       // bytes
};

struct Sequence {
    std::wstring directory;
    std::wstring prefix;     // "shot_v001."
    std::wstring suffix;     // ".exr"
    int padding = 0;         // 0 = single image
    bool padded = false;     // frame numbers keep their width ("0042")
    std::vector<SequenceFrame> frames;  // sorted by number, gaps allowed

    bool empty() const { return frames.empty(); }
    int count() const { return (int)frames.size(); }
    std::wstring displayName() const;   // "shot_v001.####.exr"
    int indexOfPath(const std::wstring& path) const;
    int indexOfNumber(int number) const;   // exact or closest frame
};

// Accepts a file (detects sibling frames) or a folder (first sequence inside).
// startIndex receives the index of the opened file within the sequence.
Sequence DetectSequence(const std::wstring& path, int* startIndex = nullptr);

// The frames of `seq` as they are on disk now (same naming); empty when none are left.
Sequence RescanSequence(const Sequence& seq);

// All image sequences (2+ frames) in a folder, optionally including subfolders.
std::vector<Sequence> FindSequences(const std::wstring& root, bool recursive);

// Base name for outputs: "shot_v001.####.exr" -> "shot_v001".
std::wstring SequenceBaseName(const Sequence& seq);

// The file of `other` for each frame of `seq` ("" = none), matched by frame number;
// sequences with unrelated numbering are matched by position.
std::vector<std::wstring> MatchFrames(const Sequence& seq, const Sequence& other);

// Versions: a "v<digits>" token in the folder or file name ("shot_v002.1001.exr",
// "renders\v002\shot.1001.exr"). The last token of the path identifies the version.
struct SequenceVersion {
    std::wstring token;      // "v003", as written on disk
    int number = 0;
    std::wstring path;       // first frame
    int frames = 0;
    bool current = false;
};
std::wstring VersionToken(const Sequence& seq);   // "" = no version in the name (no disk access)
// Every version of the sequence found next to it, oldest first (includes the current one).
std::vector<SequenceVersion> FindVersions(const Sequence& seq);
// `path` with each occurrence of the version token `from` replaced by `to`.
std::wstring ReplaceVersion(const std::wstring& path, const std::wstring& from, const std::wstring& to);
