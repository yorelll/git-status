#include "ipc_server.h"
#include "../common/ipc.h"
#include <windows.h>
#include <vector>

namespace gs {

IpcServer::IpcServer(CacheManager& mgr) : mgr_(mgr) {}
IpcServer::~IpcServer() { Stop(); }

void IpcServer::Start() {
    if (!clientSlots_) {
        clientSlots_ = CreateSemaphoreW(nullptr, 64, 64, nullptr);  // 并发请求上限
    }
    thread_ = std::thread(&IpcServer::ListenerLoop, this);
}
void IpcServer::Stop() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
    // 注意：不能在此 CloseHandle(clientSlots_)。detached 处理线程在 Stop() 后仍可能
    // 调用 ReleaseSemaphore，提前关闭句柄会造成 use-after-close（句柄可能被系统复用）。
    // 本进程是生命周期单例（由 taskkill 终止），信号量句柄由系统在进程退出时统一回收，
    // 泄漏一个句柄无害；这也使 Stop() 后再 Start() 能安全复用同一信号量。
}

// 带超时的整段读取（避免连接方只发半帧就挂起）
static bool ReadExactly(HANDLE h, void* buf, DWORD len, DWORD timeoutMs) {
    char* p = (char*)buf;
    DWORD got = 0;
    DWORD start = GetTickCount();
    while (got < len) {
        DWORD avail = 0;
        if (!PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr)) return false;
        if (avail == 0) {
            if (GetTickCount() - start > timeoutMs) return false;
            Sleep(10);
            continue;
        }
        DWORD n = 0;
        if (!ReadFile(h, p + got, len - got, &n, nullptr)) return false;
        got += n;
    }
    return true;
}

void IpcServer::ListenerLoop() {
    while (!stop_.load()) {
        HANDLE h = CreateNamedPipeW(
            PipeName().c_str(), PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES, 8192, 8192, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) { Sleep(50); continue; }
        BOOL ok = ConnectNamedPipe(h, nullptr);
        if (!ok && GetLastError() != ERROR_PIPE_CONNECTED) {
            CloseHandle(h);
            Sleep(20);  // 防错误路径忙等
            continue;
        }
        if (!clientSlots_ || WaitForSingleObject(clientSlots_, 0) != WAIT_OBJECT_0) {
            uint8_t busy = (uint8_t)StatusKind::Unknown;
            DWORD written = 0;
            WriteFile(h, &busy, 1, &written, nullptr);
            FlushFileBuffers(h);
            DisconnectNamedPipe(h);
            CloseHandle(h);
            continue;
        }
        HANDLE slots = clientSlots_;
        std::thread([this, h, slots]() {
            HandleClient(h);
            if (slots) ReleaseSemaphore(slots, 1, nullptr);
        }).detach();
    }
}

void IpcServer::HandleClient(HANDLE pipe) {
    RequestHeader hdr{};
    if (!ReadExactly(pipe, &hdr, sizeof(hdr), 2000)) { CloseHandle(pipe); return; }

    if ((hdr.type != kIpcQueryStatus && hdr.type != kIpcRegisterRepo) ||
        hdr.pathLen < sizeof(wchar_t) || hdr.pathLen > kMaxPathBytes ||
        (hdr.pathLen % sizeof(wchar_t)) != 0) {
        CloseHandle(pipe);
        return;
    }

    std::vector<wchar_t> pathBuf(hdr.pathLen / sizeof(wchar_t) + 1, 0);
    if (!ReadExactly(pipe, pathBuf.data(), hdr.pathLen, 2000)) { CloseHandle(pipe); return; }
    size_t pathChars = hdr.pathLen / sizeof(wchar_t);
    if (pathChars == 0 || pathBuf[pathChars - 1] != L'\0') { CloseHandle(pipe); return; }
    std::wstring path(pathBuf.data(), pathChars - 1);

    uint8_t resp = (uint8_t)StatusKind::NotRepo;
    if (hdr.type == kIpcQueryStatus) {
        resp = (int8_t)mgr_.Query(path);
    } else if (hdr.type == kIpcRegisterRepo) {
        mgr_.RegisterRepo(path);
        resp = 0;
    }

    DWORD written = 0;
    WriteFile(pipe, &resp, 1, &written, nullptr);
    FlushFileBuffers(pipe);
    DisconnectNamedPipe(pipe);
    CloseHandle(pipe);
}

}  // namespace gs
