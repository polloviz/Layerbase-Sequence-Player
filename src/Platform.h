#pragma once
#include <cstdint>
#include <string>
#include <vector>

typedef struct HWND__* HWND;

std::string  ToUtf8(const std::wstring& w);
std::wstring FromUtf8(const std::string& s);
std::wstring ToLower(std::wstring s);

std::wstring GetExePath();
std::wstring GetAppDataDir();          // %APPDATA%\SequencePlayer, or <exe dir>\data when portable (created on demand)
bool         IsPortable();             // portable.txt next to the exe
std::wstring GetFileExtension(const std::wstring& path);   // lowercase, with dot
std::wstring GetFileName(const std::wstring& path);
std::wstring GetParentDir(const std::wstring& path);
bool         IsDirectory(const std::wstring& path);
bool         FileExists(const std::wstring& path);
uint64_t     GetPhysicalMemoryBytes();

// Native dialogs (COM initialized lazily).
std::wstring ShowOpenFileDialog(HWND owner, bool pickFolder, const wchar_t* title);
std::wstring ShowOpenOcioDialog(HWND owner, const wchar_t* title);
std::vector<std::wstring> ShowOpenImagesDialog(HWND owner, const wchar_t* title);   // multi-select
std::wstring ShowOpenFilteredDialog(HWND owner, const wchar_t* title, const wchar_t* filterName, const wchar_t* filterSpec);
std::wstring ShowSaveFileDialog(HWND owner, const wchar_t* title, const std::wstring& initialPath,
                                const wchar_t* filterName, const wchar_t* filterSpec, const wchar_t* defaultExt);

// File association management (per-user, HKCU, no admin required).
bool RegisterFileAssociations();
bool UnregisterFileAssociations();
bool AreFileAssociationsRegistered();
void OpenDefaultAppsSettings();

// Raw bytes of an RCDATA resource embedded in the exe.
std::string LoadResourceData(int id);
void OpenUrl(const wchar_t* url);
// Opens the folder in File Explorer with the file selected.
void RevealInExplorer(const std::wstring& path);
// Puts an image on the clipboard: packed 8-bit RGB, top row first, as a DIB and,
// when png is not empty, also as "PNG" (kept lossless by apps that read it).
bool SetClipboardImage(HWND owner, const uint8_t* rgb, int width, int height, const std::string& png);
// HTTPS GET (WinHTTP, system proxy settings). Returns false on network/HTTP errors.
bool HttpGet(const std::wstring& url, std::string& body, int timeoutMs);

// Debug log to %TEMP%\SequencePlayer.log, enabled by env SP_LOG=1. Includes ms since start.
void Log(const char* fmt, ...);

// Windows UI language: true if Italian.
bool IsSystemLanguageItalian();
