#include "App.h"
#include "Platform.h"

#include <windows.h>
#include <shellapi.h>

#include <cwchar>
#include <memory>

// Usage:
//   SequencePlayer.exe [--fps <rate>] [--play] [--proxy 2|4] [--config <file.ocio | ocio://name>] [--display <name>] [--view <name>] [path]
//   SequencePlayer.exe <path> --export <out.mp4|out.mov> [--codec h264|h265|prores-proxy|prores-lt|prores|prores-hq|prores-4444] [--nvenc] [--alpha]
//   SequencePlayer.exe --batch <folder> [--out-dir <folder>] [--codec ...] [--nvenc] [--overwrite]   (recursive)
//   EXR: [--layer <name>] [--crypto-layer CryptoObject] [--matte ids|overlay|masked|matte] [--select name1,name2]
//        [--crypto-seq <frame of a Cryptomatte sequence>]   (external matte, matched by frame number)
//   SequencePlayer.exe --register | --unregister     (used by the installer)
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring path;
    double fps = 0.0;
    bool play = false;
    std::wstring config;
    StartupOptions opts;
    for (int i = 1; i < argc; ++i) {
        const std::wstring a = argv[i];
        if (a == L"--register") return RegisterFileAssociations() ? 0 : 1;
        if (a == L"--unregister") return UnregisterFileAssociations() ? 0 : 1;
        if (a == L"--fps" && i + 1 < argc) fps = _wtof(argv[++i]);
        else if (a == L"--play") play = true;
        else if (a == L"--config" && i + 1 < argc) config = argv[++i];
        else if (a == L"--display" && i + 1 < argc) opts.display = ToUtf8(argv[++i]);
        else if (a == L"--view" && i + 1 < argc) opts.view = ToUtf8(argv[++i]);
        else if (a == L"--export" && i + 1 < argc) opts.exportPath = argv[++i];
        else if (a == L"--codec" && i + 1 < argc) opts.exportCodec = ToUtf8(argv[++i]);
        else if (a == L"--nvenc") opts.exportHardware = true;
        else if (a == L"--alpha") opts.exportAlpha = true;
        else if (a == L"--batch" && i + 1 < argc) opts.batchRoot = argv[++i];
        else if (a == L"--out-dir" && i + 1 < argc) opts.batchOutDir = argv[++i];
        else if (a == L"--overwrite") opts.overwrite = true;
        else if (a == L"--proxy" && i + 1 < argc) opts.proxy = _wtoi(argv[++i]);
        else if (a == L"--layer" && i + 1 < argc) opts.layer = ToUtf8(argv[++i]);
        else if (a == L"--matte" && i + 1 < argc) opts.matte = ToUtf8(argv[++i]);
        else if (a == L"--crypto-layer" && i + 1 < argc) opts.cryptoLayer = ToUtf8(argv[++i]);
        else if (a == L"--select" && i + 1 < argc) opts.cryptoSelect = ToUtf8(argv[++i]);
        else if (a == L"--crypto-seq" && i + 1 < argc) opts.matteSeq = argv[++i];
        else if (path.empty()) path = a;
    }
    LocalFree(argv);

    // App is large (holds caches/threads); keep it off the stack.
    auto app = std::make_unique<App>();
    opts.config = ToUtf8(config);
    return app->run(path, fps, play, opts);
}
