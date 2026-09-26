#pragma once
#include <string>

// Update manifest published at APP_UPDATE_URL_W (see installer/version.json):
// {"version": "1.3.0", "url": "https://...", "notes_en": "...", "notes_it": "..."}
struct UpdateInfo {
    std::string version, url, notesEn, notesIt;
};

// Downloads and parses the manifest. Blocking (a few seconds at most): call from a worker thread.
bool FetchUpdateInfo(UpdateInfo& out);

// Compares dotted versions numerically ("1.10.0" > "1.9.2"): <0, 0, >0.
int CompareVersions(const std::string& a, const std::string& b);
