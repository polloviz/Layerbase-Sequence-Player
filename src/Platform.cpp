#include "Platform.h"
#include "ImageIO.h"
#include "Version.h"

#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <winhttp.h>
#include <algorithm>
#include <cstring>
#include <cwctype>
#include <cstdarg>
#include <cstdio>
#include <mutex>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "winhttp.lib")

std::string ToUtf8(const std::wstring& w)
{
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring FromUtf8(const std::string& s)
{
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

std::wstring ToLower(std::wstring s)
{
    for (auto& c : s) c = (wchar_t)std::towlower(c);
    return s;
}

std::wstring GetExePath()
{
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)std::size(buf));
    return std::wstring(buf, n);
}

bool IsPortable()
{
    static const bool portable = GetFileAttributesW((GetParentDir(GetExePath()) + L"\\portable.txt").c_str()) != INVALID_FILE_ATTRIBUTES;
    return portable;
}

std::wstring GetAppDataDir()
{
    // Portable: settings in a "data" folder next to the exe (unless it is read-only).
    if (IsPortable()) {
        const std::wstring dir = GetParentDir(GetExePath()) + L"\\data";
        CreateDirectoryW(dir.c_str(), nullptr);
        const DWORD a = GetFileAttributesW(dir.c_str());
        if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) {
            const std::wstring probe = dir + L"\\.write_test";
            HANDLE h = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_FLAG_DELETE_ON_CLOSE, nullptr);
            if (h != INVALID_HANDLE_VALUE) {
                CloseHandle(h);
                return dir;
            }
        }
    }
    PWSTR p = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &p))) {
        dir = std::wstring(p) + L"\\SequencePlayer";
        CoTaskMemFree(p);
        CreateDirectoryW(dir.c_str(), nullptr);
    }
    return dir;
}

std::wstring GetFileExtension(const std::wstring& path)
{
    size_t slash = path.find_last_of(L"\\/");
    size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash)) return {};
    return ToLower(path.substr(dot));
}

std::wstring GetFileName(const std::wstring& path)
{
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

std::wstring GetParentDir(const std::wstring& path)
{
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

bool IsDirectory(const std::wstring& path)
{
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool FileExists(const std::wstring& path)
{
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

uint64_t GetPhysicalMemoryBytes()
{
    MEMORYSTATUSEX ms{ sizeof(ms) };
    GlobalMemoryStatusEx(&ms);
    return ms.ullTotalPhys;
}

// ---------------------------------------------------------------------------
// Dialogs

static void EnsureCom()
{
    static bool done = false;
    if (!done) {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        done = true;
    }
}

static std::wstring RunDialog(HWND owner, bool folder, const wchar_t* title,
                              const COMDLG_FILTERSPEC* specs, UINT nspecs)
{
    EnsureCom();
    std::wstring result;
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg))))
        return result;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    opts |= FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST;
    if (folder) opts |= FOS_PICKFOLDERS;
    dlg->SetOptions(opts);
    if (title) dlg->SetTitle(title);
    if (!folder && specs && nspecs) dlg->SetFileTypes(nspecs, specs);
    if (SUCCEEDED(dlg->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            PWSTR p = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
                result = p;
                CoTaskMemFree(p);
            }
            item->Release();
        }
    }
    dlg->Release();
    return result;
}

std::wstring ShowOpenFileDialog(HWND owner, bool pickFolder, const wchar_t* title)
{
    std::wstring pattern;
    for (auto& e : SupportedExtensions()) {
        if (!pattern.empty()) pattern += L";";
        pattern += L"*" + e;
    }
    COMDLG_FILTERSPEC specs[] = { { L"Images", pattern.c_str() }, { L"*", L"*.*" } };
    return RunDialog(owner, pickFolder, title, specs, 2);
}

std::wstring ShowOpenOcioDialog(HWND owner, const wchar_t* title)
{
    COMDLG_FILTERSPEC specs[] = { { L"OCIO config", L"*.ocio;*.ocioz" }, { L"*", L"*.*" } };
    return RunDialog(owner, false, title, specs, 2);
}

std::vector<std::wstring> ShowOpenImagesDialog(HWND owner, const wchar_t* title)
{
    EnsureCom();
    std::vector<std::wstring> result;
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg))))
        return result;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_ALLOWMULTISELECT);
    if (title) dlg->SetTitle(title);
    std::wstring pattern;
    for (auto& e : SupportedExtensions()) pattern += (pattern.empty() ? L"*" : L";*") + e;
    COMDLG_FILTERSPEC specs[] = { { L"Images", pattern.c_str() }, { L"*", L"*.*" } };
    dlg->SetFileTypes(2, specs);
    if (SUCCEEDED(dlg->Show(owner))) {
        IShellItemArray* items = nullptr;
        if (SUCCEEDED(dlg->GetResults(&items))) {
            DWORD n = 0;
            items->GetCount(&n);
            for (DWORD i = 0; i < n; ++i) {
                IShellItem* item = nullptr;
                if (FAILED(items->GetItemAt(i, &item))) continue;
                PWSTR p = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
                    result.push_back(p);
                    CoTaskMemFree(p);
                }
                item->Release();
            }
            items->Release();
        }
    }
    dlg->Release();
    return result;
}

std::wstring ShowOpenFilteredDialog(HWND owner, const wchar_t* title, const wchar_t* filterName, const wchar_t* filterSpec)
{
    COMDLG_FILTERSPEC specs[] = { { filterName, filterSpec }, { L"*", L"*.*" } };
    return RunDialog(owner, false, title, specs, 2);
}

std::wstring ShowSaveFileDialog(HWND owner, const wchar_t* title, const std::wstring& initialPath,
                                const wchar_t* filterName, const wchar_t* filterSpec, const wchar_t* defaultExt)
{
    EnsureCom();
    std::wstring result;
    IFileSaveDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg))))
        return result;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | FOS_OVERWRITEPROMPT);
    if (title) dlg->SetTitle(title);
    COMDLG_FILTERSPEC spec{ filterName, filterSpec };
    dlg->SetFileTypes(1, &spec);
    if (defaultExt) dlg->SetDefaultExtension(defaultExt[0] == L'.' ? defaultExt + 1 : defaultExt);
    const std::wstring dir = GetParentDir(initialPath);
    if (!dir.empty()) {
        IShellItem* folder = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(dir.c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
            dlg->SetFolder(folder);
            folder->Release();
        }
    }
    dlg->SetFileName(GetFileName(initialPath).c_str());
    if (SUCCEEDED(dlg->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            PWSTR p = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
                result = p;
                CoTaskMemFree(p);
            }
            item->Release();
        }
    }
    dlg->Release();
    return result;
}

// ---------------------------------------------------------------------------
// File associations
//
// Layout (all under HKCU\Software\Classes unless stated):
//   SequencePlayer.<ext>                       ProgID with icon + open verb
//   .<ext>\OpenWithProgids\SequencePlayer.<ext>  -> shows in "Open with"
//   Applications\SequencePlayer.exe            -> app entry + SupportedTypes
//   HKCU\Software\SequencePlayer\Capabilities  -> Default Apps page
//   HKCU\Software\RegisteredApplications       -> link to Capabilities
// If an extension has no handler at all, we also become its default.

static bool SetRegString(HKEY root, const std::wstring& key, const wchar_t* name, const std::wstring& value)
{
    HKEY h;
    if (RegCreateKeyExW(root, key.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &h, nullptr) != ERROR_SUCCESS)
        return false;
    LONG r = RegSetValueExW(h, name, 0, REG_SZ, (const BYTE*)value.c_str(), DWORD((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(h);
    return r == ERROR_SUCCESS;
}

static bool SetRegNone(HKEY root, const std::wstring& key, const std::wstring& name)
{
    HKEY h;
    if (RegCreateKeyExW(root, key.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &h, nullptr) != ERROR_SUCCESS)
        return false;
    LONG r = RegSetValueExW(h, name.c_str(), 0, REG_NONE, nullptr, 0);
    RegCloseKey(h);
    return r == ERROR_SUCCESS;
}

static std::wstring GetRegString(HKEY root, const std::wstring& key, const wchar_t* name)
{
    wchar_t buf[1024];
    DWORD size = sizeof(buf);
    if (RegGetValueW(root, key.c_str(), name, RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS)
        return buf;
    return {};
}

static std::wstring FormatDescription(const std::wstring& ext)
{
    std::wstring up = ext.substr(1);
    for (auto& c : up) c = (wchar_t)std::towupper(c);
    return up + L" Image";
}

bool RegisterFileAssociations()
{
    const std::wstring exe = GetExePath();
    const std::wstring cmd = L"\"" + exe + L"\" \"%1\"";
    const std::wstring icon = L"\"" + exe + L"\",-1";       // app icon (resource 1)
    const std::wstring fileIcon = L"\"" + exe + L"\",-2";   // document icon (resource 2)
    const std::wstring classes = L"Software\\Classes\\";
    const std::wstring app = classes + L"Applications\\" APP_EXE_W;
    const std::wstring caps = L"Software\\SequencePlayer\\Capabilities";
    bool ok = true;

    ok &= SetRegString(HKEY_CURRENT_USER, app, L"FriendlyAppName", APP_NAME_W);
    ok &= SetRegString(HKEY_CURRENT_USER, app + L"\\DefaultIcon", nullptr, icon);
    ok &= SetRegString(HKEY_CURRENT_USER, app + L"\\shell\\open\\command", nullptr, cmd);

    ok &= SetRegString(HKEY_CURRENT_USER, caps, L"ApplicationName", APP_NAME_W);
    ok &= SetRegString(HKEY_CURRENT_USER, caps, L"ApplicationDescription",
                       L"Fast image sequence player with OpenColorIO / ACES support, by " APP_PUBLISHER_W);
    ok &= SetRegString(HKEY_CURRENT_USER, L"Software\\RegisteredApplications", L"SequencePlayer", caps);

    for (const auto& ext : SupportedExtensions()) {
        const std::wstring progId = std::wstring(APP_PROGID_PREFIX) + ext;   // SequencePlayer.exr
        const std::wstring pk = classes + progId;
        ok &= SetRegString(HKEY_CURRENT_USER, pk, nullptr, FormatDescription(ext));
        ok &= SetRegString(HKEY_CURRENT_USER, pk, L"FriendlyTypeName", FormatDescription(ext));
        ok &= SetRegString(HKEY_CURRENT_USER, pk + L"\\DefaultIcon", nullptr, fileIcon);
        ok &= SetRegString(HKEY_CURRENT_USER, pk + L"\\shell\\open", L"FriendlyAppName", APP_NAME_W);
        ok &= SetRegString(HKEY_CURRENT_USER, pk + L"\\shell\\open\\command", nullptr, cmd);

        ok &= SetRegNone(HKEY_CURRENT_USER, classes + ext + L"\\OpenWithProgids", progId);
        ok &= SetRegString(HKEY_CURRENT_USER, app + L"\\SupportedTypes", ext.c_str(), L"");
        ok &= SetRegString(HKEY_CURRENT_USER, caps + L"\\FileAssociations", ext.c_str(), progId);

        // Claim extensions nobody handles yet (typical for .exr / .dpx).
        std::wstring current = GetRegString(HKEY_CLASSES_ROOT, ext, nullptr);
        if (current.empty())
            SetRegString(HKEY_CURRENT_USER, classes + ext, nullptr, progId);
    }

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return ok;
}

bool UnregisterFileAssociations()
{
    const std::wstring classes = L"Software\\Classes\\";
    for (const auto& ext : SupportedExtensions()) {
        const std::wstring progId = std::wstring(APP_PROGID_PREFIX) + ext;
        RegDeleteTreeW(HKEY_CURRENT_USER, (classes + progId).c_str());
        HKEY h;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, (classes + ext + L"\\OpenWithProgids").c_str(), 0, KEY_WRITE, &h) == ERROR_SUCCESS) {
            RegDeleteValueW(h, progId.c_str());
            RegCloseKey(h);
        }
        if (GetRegString(HKEY_CURRENT_USER, classes + ext, nullptr) == progId) {
            if (RegOpenKeyExW(HKEY_CURRENT_USER, (classes + ext).c_str(), 0, KEY_WRITE, &h) == ERROR_SUCCESS) {
                RegDeleteValueW(h, nullptr);
                RegCloseKey(h);
            }
        }
    }
    RegDeleteTreeW(HKEY_CURRENT_USER, (classes + L"Applications\\" APP_EXE_W).c_str());
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\SequencePlayer\\Capabilities");
    HKEY h;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\RegisteredApplications", 0, KEY_WRITE, &h) == ERROR_SUCCESS) {
        RegDeleteValueW(h, L"SequencePlayer");
        RegCloseKey(h);
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
}

bool AreFileAssociationsRegistered()
{
    std::wstring cmd = GetRegString(HKEY_CURRENT_USER,
        L"Software\\Classes\\Applications\\" APP_EXE_W L"\\shell\\open\\command", nullptr);
    return !cmd.empty() && cmd.find(GetExePath()) != std::wstring::npos;
}

void OpenDefaultAppsSettings()
{
    ShellExecuteW(nullptr, L"open", L"ms-settings:defaultapps?registeredAppUser=SequencePlayer",
                  nullptr, nullptr, SW_SHOWNORMAL);
}

bool IsSystemLanguageItalian()
{
    return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_ITALIAN;
}

void Log(const char* fmt, ...)
{
    static const bool enabled = [] { wchar_t b[8]; return GetEnvironmentVariableW(L"SP_LOG", b, 8) > 0; }();
    if (!enabled) return;
    static std::mutex mtx;
    static LARGE_INTEGER freq, start;
    static FILE* f = [] {
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&start);
        wchar_t tmp[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        return _wfopen((std::wstring(tmp) + L"SequencePlayer.log").c_str(), L"w");
    }();
    if (!f) return;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    std::lock_guard lock(mtx);
    fprintf(f, "[%8.2f] ", double(now.QuadPart - start.QuadPart) * 1000.0 / double(freq.QuadPart));
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fflush(f);
}

std::string LoadResourceData(int id)
{
    HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!r) return {};
    HGLOBAL g = LoadResource(nullptr, r);
    const void* p = g ? LockResource(g) : nullptr;
    return p ? std::string(static_cast<const char*>(p), SizeofResource(nullptr, r)) : std::string();
}

void OpenUrl(const wchar_t* url)
{
    ShellExecuteW(nullptr, L"open", url, nullptr, nullptr, SW_SHOWNORMAL);
}

void RevealInExplorer(const std::wstring& path)
{
    EnsureCom();
    if (PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(path.c_str())) {
        const HRESULT hr = SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
        ILFree(pidl);
        if (SUCCEEDED(hr)) return;
    }
    ShellExecuteW(nullptr, L"open", L"explorer.exe", (L"/select,\"" + path + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
}

bool SetClipboardImage(HWND owner, const uint8_t* rgb, int width, int height, const std::string& png)
{
    if (!rgb || width <= 0 || height <= 0) return false;
    // CF_DIB: bottom-up BGR rows padded to 4 bytes.
    const size_t stride = (size_t(width) * 3 + 3) & ~size_t(3);
    const size_t size = sizeof(BITMAPINFOHEADER) + stride * height;
    HGLOBAL dib = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!dib) return false;
    auto* p = static_cast<uint8_t*>(GlobalLock(dib));
    BITMAPINFOHEADER bih{ sizeof(bih) };
    bih.biWidth = width;
    bih.biHeight = height;
    bih.biPlanes = 1;
    bih.biBitCount = 24;
    bih.biCompression = BI_RGB;
    bih.biSizeImage = DWORD(stride * height);
    memcpy(p, &bih, sizeof(bih));
    for (int y = 0; y < height; ++y) {
        const uint8_t* src = rgb + size_t(height - 1 - y) * width * 3;
        uint8_t* dst = p + sizeof(bih) + stride * y;
        for (int x = 0; x < width; ++x) {
            dst[x * 3 + 0] = src[x * 3 + 2];
            dst[x * 3 + 1] = src[x * 3 + 1];
            dst[x * 3 + 2] = src[x * 3 + 0];
        }
        memset(dst + size_t(width) * 3, 0, stride - size_t(width) * 3);
    }
    GlobalUnlock(dib);

    if (!OpenClipboard(owner)) {
        GlobalFree(dib);
        return false;
    }
    EmptyClipboard();
    bool ok = SetClipboardData(CF_DIB, dib) != nullptr;
    if (!ok) GlobalFree(dib);
    if (ok && !png.empty()) {
        if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, png.size())) {
            memcpy(GlobalLock(h), png.data(), png.size());
            GlobalUnlock(h);
            if (!SetClipboardData(RegisterClipboardFormatW(L"PNG"), h)) GlobalFree(h);
        }
    }
    CloseClipboard();
    return ok;
}

bool HttpGet(const std::wstring& url, std::string& body, int timeoutMs)
{
    body.clear();
    URL_COMPONENTS uc{ sizeof(uc) };
    wchar_t host[256] = {}, path[2048] = {};
    uc.lpszHostName = host;
    uc.dwHostNameLength = (DWORD)std::size(host);
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = (DWORD)std::size(path);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return false;

    const std::wstring agent = std::wstring(APP_NAME_W) + L"/" + FromUtf8(APP_VERSION);
    HINTERNET session = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;
    WinHttpSetTimeouts(session, timeoutMs, timeoutMs, timeoutMs, timeoutMs);
    bool ok = false;
    if (HINTERNET conn = WinHttpConnect(session, host, uc.nPort, 0)) {
        const DWORD flags = uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
        if (HINTERNET req = WinHttpOpenRequest(conn, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags)) {
            if (WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(req, nullptr)) {
                DWORD status = 0, size = sizeof(status);
                WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                                    &status, &size, WINHTTP_NO_HEADER_INDEX);
                ok = status == 200;
                DWORD avail = 0;
                while (ok && WinHttpQueryDataAvailable(req, &avail) && avail > 0 && body.size() < (1u << 20)) {
                    std::string chunk(avail, '\0');
                    DWORD got = 0;
                    if (!WinHttpReadData(req, chunk.data(), avail, &got)) { ok = false; break; }
                    body.append(chunk.data(), got);
                }
            }
            WinHttpCloseHandle(req);
        }
        WinHttpCloseHandle(conn);
    }
    WinHttpCloseHandle(session);
    return ok;
}
