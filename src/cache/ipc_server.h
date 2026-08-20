#pragma once
#include <windows.h>
#include <atomic>
#include <thread>
#include "cache_manager.h"

namespace gs {

// 命名管道服务：监听、每连接一线程、短连接（查询→响应→断开）。
class IpcServer {
public:
    explicit IpcServer(CacheManager& mgr);
    ~IpcServer();
    void Start();
    void Stop();

private:
    void ListenerLoop();
    void HandleClient(HANDLE pipe);

    CacheManager& mgr_;
    std::atomic<bool> stop_{false};
    HANDLE clientSlots_ = nullptr;
    std::thread thread_;
};

}  // namespace gs
