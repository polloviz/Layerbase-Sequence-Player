#pragma once
#include <map>
#include <string>
#include <vector>

// Persisted in %APPDATA%\SequencePlayer\settings.ini (UTF-8, key=value).
struct Settings {
    std::string language = "auto";          // auto | en | it
    double defaultFps = 30.0;
    int cacheMB = 0;                         // 0 = automatic (40% of RAM, max 16 GB)
    int loopMode = 0;                        // 0 loop, 1 once, 2 ping-pong

    std::string configSource;                // "" = default built-in ACES 2.0
    std::vector<std::string> recentConfigs;  // custom .ocio paths
    std::string display, view, look;
    // Last input chosen per (config source, format class), key "source|float" / "source|int".
    std::map<std::string, std::string> inputMemory;
    std::vector<std::string> recentFiles;
    std::vector<std::string> recentLuts;     // LUT files, most recent first (none is loaded at startup)
    int lutPosition = 0;                     // LutPosition
    bool confirmReplace = true;              // ask before another sequence replaces the open one
    std::string frameSaveDir;                // last folder of "Save frame as"
    std::string frameSaveExt = ".png";
    bool liveRefresh = true;                 // follow the folder of the open sequence while it renders
    // Viewer guides
    int guideAspect = 0;                     // AspectRatio index; 0 = none
    bool guideSafe = false, guideCenter = false, guideThirds = false;

    // Movie export
    int exportCodec = 0, exportQuality = 0, exportScale = 100;
    bool exportHardware = false;
    bool exportPremultiplied = false;        // ProRes 4444 alpha: premultiplied color
    int exportAspect = 0;                    // AspectRatio index; 0 = full frame
    bool exportAspectBars = false;
    bool burnIn = false, burnName = true, burnFrame = true, burnTimecode = false, burnDate = false;
    std::string burnText;
    std::string ffmpegPath;                  // "" = auto-detect
    // Batch conversion
    int batchDest = 0;                       // 0 next to each sequence, 1 folder
    std::string batchFolder;
    bool batchSkipExisting = true, batchRecursive = true, batchCurrentInput = false, batchUseLayer = true;

    // Update check (one small HTTPS request at most once a day)
    bool checkUpdates = true;
    long long lastUpdateCheck = 0;           // unix time
    std::string skipVersion;                 // "Skip this version"

    int winX = -1, winY = -1, winW = 1280, winH = 760;
    bool winMaximized = false;

    void load();
    void save() const;
    static void PushRecent(std::vector<std::string>& list, const std::string& item, size_t max = 10);
};
