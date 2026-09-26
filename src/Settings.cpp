#include "Settings.h"
#include "Platform.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

static std::wstring SettingsPath() { return GetAppDataDir() + L"\\settings.ini"; }

void Settings::load()
{
    std::ifstream f(SettingsPath());
    if (!f) return;
    std::string line;
    recentFiles.clear();
    recentConfigs.clear();
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        auto toInt = [&](int def) { return v.empty() ? def : std::atoi(v.c_str()); };
        if (k == "language") language = v;
        else if (k == "defaultFps") defaultFps = std::clamp(std::atof(v.c_str()), 1.0, 240.0);
        else if (k == "cacheMB") cacheMB = std::max(0, toInt(0));
        else if (k == "loopMode") loopMode = std::clamp(toInt(0), 0, 2);
        else if (k == "configSource") configSource = v;
        else if (k == "recentConfig") recentConfigs.push_back(v);
        else if (k == "display") display = v;
        else if (k == "view") view = v;
        else if (k == "look") look = v;
        else if (k == "inputMem") {
            const size_t tab = v.find('\t');
            if (tab != std::string::npos) inputMemory[v.substr(0, tab)] = v.substr(tab + 1);
        }
        else if (k == "recentFile") recentFiles.push_back(v);
        else if (k == "exportCodec") exportCodec = std::clamp(toInt(0), 0, 6);
        else if (k == "exportQuality") exportQuality = std::clamp(toInt(0), 0, 2);
        else if (k == "exportScale") exportScale = std::clamp(toInt(100), 10, 100);
        else if (k == "exportHardware") exportHardware = toInt(0) != 0;
        else if (k == "ffmpegPath") ffmpegPath = v;
        else if (k == "checkUpdates") checkUpdates = toInt(1) != 0;
        else if (k == "lastUpdateCheck") { try { lastUpdateCheck = std::stoll(v); } catch (...) {} }
        else if (k == "skipVersion") skipVersion = v;
        else if (k == "batchDest") batchDest = std::clamp(toInt(0), 0, 1);
        else if (k == "batchFolder") batchFolder = v;
        else if (k == "batchSkipExisting") batchSkipExisting = toInt(1) != 0;
        else if (k == "batchRecursive") batchRecursive = toInt(1) != 0;
        else if (k == "batchCurrentInput") batchCurrentInput = toInt(0) != 0;
        else if (k == "batchUseLayer") batchUseLayer = toInt(1) != 0;
        else if (k == "winX") winX = toInt(-1);
        else if (k == "winY") winY = toInt(-1);
        else if (k == "winW") winW = std::max(400, toInt(1280));
        else if (k == "winH") winH = std::max(300, toInt(760));
        else if (k == "winMaximized") winMaximized = toInt(0) != 0;
    }
}

void Settings::save() const
{
    std::ostringstream o;
    o << "language=" << language << "\n"
      << "defaultFps=" << defaultFps << "\n"
      << "cacheMB=" << cacheMB << "\n"
      << "loopMode=" << loopMode << "\n"
      << "configSource=" << configSource << "\n"
      << "display=" << display << "\n"
      << "view=" << view << "\n"
      << "look=" << look << "\n"
      << "winX=" << winX << "\nwinY=" << winY << "\nwinW=" << winW << "\nwinH=" << winH << "\n"
      << "winMaximized=" << (winMaximized ? 1 : 0) << "\n"
      << "exportCodec=" << exportCodec << "\nexportQuality=" << exportQuality << "\nexportScale=" << exportScale << "\n"
      << "exportHardware=" << (exportHardware ? 1 : 0) << "\nffmpegPath=" << ffmpegPath << "\n"
      << "batchDest=" << batchDest << "\nbatchFolder=" << batchFolder << "\nbatchSkipExisting=" << (batchSkipExisting ? 1 : 0)
      << "\nbatchRecursive=" << (batchRecursive ? 1 : 0) << "\nbatchCurrentInput=" << (batchCurrentInput ? 1 : 0)
      << "\nbatchUseLayer=" << (batchUseLayer ? 1 : 0) << "\n"
      << "checkUpdates=" << (checkUpdates ? 1 : 0) << "\nlastUpdateCheck=" << lastUpdateCheck << "\nskipVersion=" << skipVersion << "\n";
    for (auto& r : recentConfigs) o << "recentConfig=" << r << "\n";
    for (auto& r : recentFiles) o << "recentFile=" << r << "\n";
    for (auto& [key, value] : inputMemory) o << "inputMem=" << key << '\t' << value << "\n";

    // Write to temp then replace, so a crash never leaves a truncated file.
    const std::wstring path = SettingsPath(), tmp = path + L".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return;
        f << o.str();
    }
    _wremove(path.c_str());
    _wrename(tmp.c_str(), path.c_str());
}

void Settings::PushRecent(std::vector<std::string>& list, const std::string& item, size_t max)
{
    list.erase(std::remove(list.begin(), list.end(), item), list.end());
    list.insert(list.begin(), item);
    if (list.size() > max) list.resize(max);
}
