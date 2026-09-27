#pragma once
#include <string>
#include <vector>

struct SequenceFrame {
    int number = 0;
    std::wstring path;
};

struct Sequence {
    std::wstring directory;
    std::wstring prefix;     // "shot_v001."
    std::wstring suffix;     // ".exr"
    int padding = 0;         // 0 = single image
    std::vector<SequenceFrame> frames;  // sorted by number, gaps allowed

    bool empty() const { return frames.empty(); }
    int count() const { return (int)frames.size(); }
    std::wstring displayName() const;   // "shot_v001.####.exr"
    int indexOfPath(const std::wstring& path) const;
};

// Accepts a file (detects sibling frames) or a folder (first sequence inside).
// startIndex receives the index of the opened file within the sequence.
Sequence DetectSequence(const std::wstring& path, int* startIndex = nullptr);

// All image sequences (2+ frames) in a folder, optionally including subfolders.
std::vector<Sequence> FindSequences(const std::wstring& root, bool recursive);

// Base name for outputs: "shot_v001.####.exr" -> "shot_v001".
std::wstring SequenceBaseName(const Sequence& seq);

// The file of `other` for each frame of `seq` ("" = none), matched by frame number;
// sequences with unrelated numbering are matched by position.
std::vector<std::wstring> MatchFrames(const Sequence& seq, const Sequence& other);
