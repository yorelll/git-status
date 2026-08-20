#include "dir_watcher.h"
#include "../common/pathutil.h"
#include <windows.h>
#include <vector>

namespace gs {

DirWatcher::DirWatcher(std::wstring root, Callback cb)
    : root_(std::move(root)), cb_(std::move(cb)) {
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    dirHandle_ = CreateFileW(root_.c_str(), FILE_LIST_DIRECTORY,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
}

DirWatcher::~DirWatcher() {
    RequestStop();
    if (thread_.joinable()) thread_.join();
    if (dirHandle_ != INVALID_HANDLE_VALUE) CloseHandle(dirHandle_);
    if (stopEvent_) CloseHandle(stopEvent_);
}

void DirWatcher::Start() {
    if (dirHandle_ == INVALID_HANDLE_VALUE) return;
    thread_ = std::thread(&DirWatcher::ThreadMain, this);
}

void DirWatcher::RequestStop() {
    stop_ = true;
    if (stopEvent_) SetEvent(stopEvent_);
}

void DirWatcher::ThreadMain() {
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE waiters[2] = { stopEvent_, ov.hEvent };
    std::vector<uint8_t> buf(64 * 1024);

    while (!stop_.load()) {
        ResetEvent(ov.hEvent);
        DWORD bytes = 0;
        BOOL ok = ReadDirectoryChangesW(
            dirHandle_, buf.data(), (DWORD)buf.size(), TRUE,
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SIZE |
                FILE_NOTIFY_CHANGE_LAST_WRITE,
            &bytes, &ov, nullptr);
        if (!ok) break;  // 目录被删/句柄失效等

        DWORD wr = WaitForMultipleObjects(2, waiters, FALSE, INFINITE);
        if (wr == WAIT_OBJECT_0 || wr == WAIT_FAILED) break;  // stop 或出错
        DWORD transferred = 0;
        if (!GetOverlappedResult(dirHandle_, &ov, &transferred, FALSE)) break;
        if (transferred == 0) continue;

        std::vector<std::wstring> pending;
        bool dotGit = false;
        const FILE_NOTIFY_INFORMATION* fni =
            reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(buf.data());
        for (;;) {
            std::wstring name(fni->FileName, fni->FileNameLength / sizeof(wchar_t));
            if (!name.empty()) {
                std::wstring rel = ToLowerW(Backslash(name));
                pending.push_back(rel);
                if (rel == L".git" || rel.rfind(L".git\\", 0) == 0) dotGit = true;
            }
            if (fni->NextEntryOffset == 0) break;
            fni = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(
                reinterpret_cast<const uint8_t*>(fni) + fni->NextEntryOffset);
        }
        if (!pending.empty()) cb_(root_, pending, dotGit);
    }
    CloseHandle(ov.hEvent);
}

}  // namespace gs
