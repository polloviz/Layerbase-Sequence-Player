#include "DirWatcher.h"

#include <windows.h>

void DirWatcher::start(const std::wstring& dir, std::function<void()> onChange)
{
    stop();
    m_dir = dir;
    HANDLE stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stopEvent) return;
    m_stop = stopEvent;
    m_thread = std::thread([dir, stopEvent, fn = std::move(onChange)] {
        // Opened here: on a network share this can take a moment.
        HANDLE change = FindFirstChangeNotificationW(dir.c_str(), FALSE,
                                                     FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE);
        if (change == INVALID_HANDLE_VALUE) return;
        const HANDLE handles[2] = { stopEvent, change };
        while (WaitForMultipleObjects(2, handles, FALSE, INFINITE) == WAIT_OBJECT_0 + 1) {
            fn();
            if (!FindNextChangeNotification(change)) break;
        }
        FindCloseChangeNotification(change);
    });
}

void DirWatcher::stop()
{
    if (m_stop) SetEvent((HANDLE)m_stop);
    if (m_thread.joinable()) m_thread.join();
    if (m_stop) CloseHandle((HANDLE)m_stop);
    m_stop = nullptr;
    m_dir.clear();
}
