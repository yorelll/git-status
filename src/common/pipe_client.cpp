#include "pipe_client.h"
#include "ipc.h"
#include "pathutil.h"
#include <vector>

namespace gs {

static HMODULE g_selfModule = nullptr;

void SetSelfModule(HMODULE h) { g_selfModule = h; }

static std::wstring CacheExePath() {
    wchar_t buf[MAX_PATH * 2] = {0};
    DWORD n = GetModuleFileNameW(g_selfModule ? g_selfModule : GetModuleHandleW(nullptr),
                                 buf, MAX_PATH * 2);
    if (n == 0 || n >= MAX_PATH * 2) return L"";
    return ParentDir(buf) + L"\\GitStatusCache.exe";
}

// 尽力拉起缓存进程（3s 节流；若已有实例则跳过）
static void SpawnCache() {
    static DWORD lastTick = 0;
    DWORD now = GetTickCount();
    if (now - lastTick < 3000) return;
    lastTick = now;

    HANDLE m = CreateMutexW(nullptr, TRUE, InstanceMutexName().c_str());
    if (m && GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(m);  // 缓存已在跑，只是管道还没就绪
        return;
    }
    if (m) CloseHandle(m);

    std::wstring exe = CacheExePath();
    if (exe.empty() || GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    std::wstring cmd = L"\"" + exe + L"\" -spawned";
    std::vector<wchar_t> c(cmd.begin(), cmd.end());
    c.push_back(0);
    STARTUPINFOW si{ sizeof(si) };
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, c.data(), nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

// 带超时的同步 IO（overlapped + 事件）
static bool OverlappedIo(HANDLE h, bool write, void* buf, DWORD len, DWORD timeoutMs) {
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ov.hEvent) return false;
    BOOL ok = write ? WriteFile(h, buf, len, nullptr, &ov)
                    : ReadFile(h, buf, len, nullptr, &ov);
    if (!ok) {
        DWORD e = GetLastError();
        if (e != ERROR_IO_PENDING) { CloseHandle(ov.hEvent); return false; }
        DWORD r = WaitForSingleObject(ov.hEvent, timeoutMs);
        if (r != WAIT_OBJECT_0) {
            CancelIo(h);
            CloseHandle(ov.hEvent);
            return false;
        }
        DWORD n = 0;
        ok = GetOverlappedResult(h, &ov, &n, FALSE);
        if (!ok || n != len) { CloseHandle(ov.hEvent); return false; }
    }
    CloseHandle(ov.hEvent);
    return true;
}

bool PipeQuery(const std::wstring& path, uint32_t type,
               StatusKind& outStatus, bool ensureRunning) {
    std::wstring pipe = PipeName();
    for (int attempt = 0; attempt < 2; ++attempt) {
        HANDLE h = CreateFileW(pipe.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                               nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            DWORD e = GetLastError();
            if (e == ERROR_PIPE_BUSY) {
                if (!WaitNamedPipeW(pipe.c_str(), 100)) {
                    if (attempt == 0 && ensureRunning) { SpawnCache(); Sleep(150); continue; }
                    return false;
                }
                continue;  // 重试 CreateFile
            }
            if (e == ERROR_FILE_NOT_FOUND) {
                if (attempt == 0 && ensureRunning) { SpawnCache(); Sleep(150); continue; }
                return false;
            }
            return false;
        }

        RequestHeader hdr{ type, (uint32_t)((path.size() + 1) * sizeof(wchar_t)) };
        DWORD bodyBytes = (DWORD)((path.size() + 1) * sizeof(wchar_t));
        if (!OverlappedIo(h, true, &hdr, sizeof(hdr), 100)) { CloseHandle(h); return false; }
        if (!OverlappedIo(h, true, (void*)path.c_str(), bodyBytes, 100)) { CloseHandle(h); return false; }

        uint8_t resp = (uint8_t)StatusKind::Unknown;
        if (!OverlappedIo(h, false, &resp, 1, 100)) { CloseHandle(h); return false; }
        outStatus = (StatusKind)resp;

        FlushFileBuffers(h);
        DisconnectNamedPipe(h);
        CloseHandle(h);
        return true;
    }
    return false;
}

}  // namespace gs
