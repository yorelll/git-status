#pragma once
#include <map>
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>
#include "../common/status.h"
#include "../engine/repo_state.h"
#include "dir_watcher.h"

namespace gs {

// 仓库状态表：注册仓库 → 初始快照 → 起监视；查询/更新走聚合状态。
class CacheManager {
public:
    CacheManager() = default;
    ~CacheManager();

    void Shutdown();
    StatusKind Query(const std::wstring& absPath) const;
    bool RegisterRepo(const std::wstring& root);
    void OnRepoChanged(const std::wstring& root, const std::vector<std::wstring>& relEvents,
                       bool dotGitTouched);

private:
    struct Repo {
        std::wstring root;                      // 小写反斜杠，无尾斜杠
        RepoState state;
        std::unique_ptr<DirWatcher> watcher;
    };
    Repo* FindRepo(const std::wstring& rootLower) const;
    void FullRescan(const std::wstring& rootLower);
    void IncrementalUpdate(const std::wstring& rootLower,
                           const std::vector<std::wstring>& pathspecs);
    static std::vector<std::wstring> TopMostEvents(const std::vector<std::wstring>& events);

    mutable std::shared_mutex mtx_;
    std::map<std::wstring, std::unique_ptr<Repo>> repos_;
    // 已停止、待线程退出的监视器（不能在回调线程内 join，移到这里延迟析构）
    std::vector<std::unique_ptr<DirWatcher>> graveyard_;
};

}  // namespace gs
