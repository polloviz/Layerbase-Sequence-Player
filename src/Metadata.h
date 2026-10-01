#pragma once
#include <string>
#include <vector>

struct MetaEntry {
    std::string key, value;
    bool section = false;   // a heading ("File", "EXR header · part 1")
};

// File details and the metadata stored in the file: every EXR header attribute (render
// time, camera, renderer settings...) of every part, TIFF tags. Reads only the header.
std::vector<MetaEntry> ReadMetadata(const std::wstring& path);
