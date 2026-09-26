#include "UpdateCheck.h"
#include "Platform.h"
#include "Version.h"

#include <algorithm>
#include <cctype>
#include <vector>

namespace {

// Value of a top-level string field in a flat JSON object ("" if missing).
std::string JsonString(const std::string& json, const char* key)
{
    const std::string needle = std::string("\"") + key + "\"";
    size_t i = json.find(needle);
    if (i == std::string::npos) return {};
    i = json.find(':', i + needle.size());
    if (i == std::string::npos) return {};
    i = json.find('"', i + 1);
    if (i == std::string::npos) return {};
    std::string out;
    for (++i; i < json.size() && json[i] != '"'; ++i) {
        char c = json[i];
        if (c == '\\' && i + 1 < json.size()) {
            c = json[++i];
            if (c == 'n') c = '\n';
            else if (c == 't') c = '\t';
        }
        out += c;
    }
    return out;
}

std::vector<int> Parts(const std::string& v)
{
    std::vector<int> out;
    int n = 0;
    bool any = false;
    for (char c : v) {
        if (std::isdigit((unsigned char)c)) { n = n * 10 + (c - '0'); any = true; }
        else if (c == '.') { out.push_back(n); n = 0; any = false; }
        else break;   // "-beta" and the like end the numeric part
    }
    if (any || out.empty()) out.push_back(n);
    return out;
}

}  // namespace

int CompareVersions(const std::string& a, const std::string& b)
{
    const std::vector<int> pa = Parts(a), pb = Parts(b);
    for (size_t i = 0; i < std::max(pa.size(), pb.size()); ++i) {
        const int x = i < pa.size() ? pa[i] : 0, y = i < pb.size() ? pb[i] : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

bool FetchUpdateInfo(UpdateInfo& out)
{
    std::string body;
    const wchar_t* test = _wgetenv(L"SP_UPDATE_URL");   // debug: alternative manifest URL
    if (!HttpGet(test ? test : APP_UPDATE_URL_W, body, 5000)) {
        Log("update check: request failed");
        return false;
    }
    out.version = JsonString(body, "version");
    out.url = JsonString(body, "url");
    out.notesEn = JsonString(body, "notes_en");
    out.notesIt = JsonString(body, "notes_it");
    Log("update check: latest %s", out.version.c_str());
    // Only web links are opened from the banner.
    if (out.url.rfind("https://", 0) != 0) out.url.clear();
    return !out.version.empty();
}
