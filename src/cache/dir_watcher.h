#pragma once
#include <windows.h>
#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace gs {

// 对仓库根目录做递归监视（ReadDirectoryChangesW，bWatchSubtree）。
// 每仓库一个实例、一个线程。变更打包回调，避免高频骚扰。
class DirWatcher {
public:
    using Callback = std::function<void(const std::wstring& root,
                                        const std::vector<std::wstring>& relEvents,
                                        bool dotGitTouched)>;
    DirWatcher(std::wstring root, Callback cb);
    ~DirWatcher();

    void Start();        // 起监视线程（构造时不启，避免注册前收到事件）
    void RequestStop();  // 线程会在下次等待返回时退出；不能从回调线程 join

private:
    void ThreadMain();

    std::wstring root_;
    Callback cb_;
    std::atomic<bool> stop_{false};
    std::thread thread_;
    HANDLE dirHandle_ = INVALID_HANDLE_VALUE;
    HANDLE stopEvent_ = nullptr;
};

}  // namespace gs
