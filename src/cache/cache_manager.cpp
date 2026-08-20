#include "cache_manager.h"
#include "../common/pathutil.h"
#include "../engine/git_status.h"
#include <algorithm>
#include <iterator>
#include <mutex>

namespace gs {

CacheManager::~CacheManager() { Shutdown(); }

void CacheManager::DrainGraveyard(std::vector<std::unique_ptr<DirWatcher>>& out) {
    std::unique_lock lk(mtx_);
    if (graveyard_.empty()) return;
    out.insert(out.end(),
               std::make_move_iterator(graveyard_.begin()),
               std::make_move_iterator(graveyard_.end()));
    graveyard_.clear();
}

void CacheManager::Shutdown() {
    std::vector<std::unique_ptr<DirWatcher>> watchers;
    {
        std::unique_lock lk(mtx_);
        for (auto& [k, r] : repos_)
            if (r->watcher) watchers.push_back(std::move(r->watcher));
        repos_.clear();
        graveyard_.clear();
    }
    // 析构触发 join（此时没有回调线程在跑，安全）
    watchers.clear();
}

CacheManager::Repo* CacheManager::FindRepo(const std::wstring& rootLower) const {
    auto it = repos_.find(rootLower);
    return it == repos_.end() ? nullptr : it->second.get();
}

StatusKind CacheManager::Query(const std::wstring& absPath) const {
    std::wstring p = TrimTrailingSlash(ToLowerW(Backslash(absPath)));
    std::shared_lock lk(mtx_);
    const Repo* bestRepo = nullptr;
    size_t bestRootLen = 0;
    for (auto& [root, repo] : repos_) {
        if (IsPathPrefix(root, p) && root.size() >= bestRootLen) {
            bestRepo = repo.get();
            bestRootLen = root.size();
        }
    }
    if (!bestRepo) return StatusKind::NotRepo;
    std::wstring rel = (p.size() > bestRootLen) ? p.substr(bestRootLen + 1) : L"";
    return bestRepo->state.StatusFor(rel);
}

bool CacheManager::RegisterRepo(const std::wstring& root) {
    // 延迟回收失效 watcher：在非监视回调线程做析构/join，避免积累。
    std::vector<std::unique_ptr<DirWatcher>> reclaim;
    DrainGraveyard(reclaim);
    reclaim.clear();

    std::wstring r = TrimTrailingSlash(ToLowerW(Backslash(AbsPath(root))));
    if (r.size() < 3 || r[1] != L':') return false;
    {
        std::shared_lock lk(mtx_);
        if (repos_.count(r)) return true;
    }
    // 先做权威快照（不持锁）
    std::vector<GitDirtyPath> dirty;
    std::wstring err;
    if (!GitStatusScan(r, {}, dirty, err)) return false;  // 非仓库或 git 失败

    auto repo = std::make_unique<Repo>();
    repo->root = r;
    repo->state.Reset(dirty);
    auto watcher = std::make_unique<DirWatcher>(
        r, [this](const std::wstring& root2, const std::vector<std::wstring>& ev, bool dotgit) {
            OnRepoChanged(root2, ev, dotgit);
        });
    {
        std::unique_lock lk(mtx_);
        if (repos_.count(r)) return true;  // 并发注册竞态
        repo->watcher = std::move(watcher);
        repos_[r] = std::move(repo);
    }
    // 注册后才启动监视
    auto it = repos_.find(r);
    if (it != repos_.end() && it->second->watcher) it->second->watcher->Start();
    return true;
}

// 把事件去重为最顶层子树（父目录已覆盖则丢弃子项）
std::vector<std::wstring> CacheManager::TopMostEvents(const std::vector<std::wstring>& events) {
    std::vector<std::wstring> v;
    v.reserve(events.size());
    for (auto& e : events) {
        if (e.empty() || e == L".git" || e.rfind(L".git\\", 0) == 0) continue;
        v.push_back(e);
    }
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    std::vector<std::wstring> tops;
    for (auto& x : v) {
        bool covered = false;
        for (auto& y : tops)
            if (IsPathPrefix(y, x)) { covered = true; break; }
        if (!covered) tops.push_back(x);
    }
    return tops;
}

void CacheManager::FullRescan(const std::wstring& rootLower) {
    std::vector<GitDirtyPath> dirty;
    std::wstring err;
    if (!GitStatusScan(rootLower, {}, dirty, err)) return;
    std::unique_lock lk(mtx_);
    Repo* r = FindRepo(rootLower);
    if (r) r->state.Reset(dirty);
}

void CacheManager::IncrementalUpdate(const std::wstring& rootLower,
                                     const std::vector<std::wstring>& pathspecs) {
    for (auto& ps : pathspecs) {
        std::vector<GitDirtyPath> dirty;
        std::wstring err;
        if (!GitStatusScan(rootLower, { ps }, dirty, err)) continue;
        std::unique_lock lk(mtx_);
        Repo* r = FindRepo(rootLower);
        if (r) r->state.ReplaceSubtree(ps, dirty);
        lk.unlock();
    }
}

void CacheManager::OnRepoChanged(const std::wstring& root,
                                 const std::vector<std::wstring>& relEvents,
                                 bool dotGitTouched) {
    std::wstring r = TrimTrailingSlash(ToLowerW(Backslash(root)));

    // .git 目录本身没了 → 仓库失效，移除
    if (!HasDotGit(r)) {
        std::unique_lock lk(mtx_);
        auto it = repos_.find(r);
        if (it != repos_.end()) {
            if (it->second->watcher) {
                it->second->watcher->RequestStop();
                graveyard_.push_back(std::move(it->second->watcher));
            }
            repos_.erase(it);
        }
        return;
    }
    if (dotGitTouched) {
        FullRescan(r);
        return;
    }
    std::vector<std::wstring> tops = TopMostEvents(relEvents);
    if (tops.empty()) return;
    IncrementalUpdate(r, tops);
}

}  // namespace gs
