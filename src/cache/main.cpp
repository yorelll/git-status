// GitStatusCache.exe —— 后台缓存进程（单实例）
// 维护各 git 仓库的状态聚合表，向资源管理器覆盖图标 DLL 提供命名管道查询。
#include <windows.h>
#include "../common/ipc.h"
#include "cache_manager.h"
#include "ipc_server.h"

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    // 单实例：已运行则直接退出（可能是 -spawned 重复拉起）
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, gs::InstanceMutexName().c_str());
    if (!hMutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (hMutex) CloseHandle(hMutex);
        return 0;
    }

    gs::CacheManager mgr;
    gs::IpcServer server(mgr);
    server.Start();

    // 常驻：无窗口、事件驱动；卸载时由 taskkill 结束（互斥体随之释放）
    HANDLE ev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    WaitForSingleObject(ev, INFINITE);
    return 0;
}
