#include "repo_state.h"
#include "../common/pathutil.h"

namespace gs {

// 统一化 key：反斜杠 + 小写 + 去尾斜杠
std::wstring RepoState::Key(const std::wstring& rel) {
    std::wstring k = Backslash(rel);
    while (!k.empty() && k.back() == L'\\') k.pop_back();
    return ToLowerW(k);
}

void RepoState::UpdateDirCounts(const std::wstring& rel, int delta) {
    if (rel.empty()) return;
    std::wstring cur = rel;
    for (;;) {
        size_t p = cur.find_last_of(L'\\');
        cur = (p == std::wstring::npos) ? L"" : cur.substr(0, p);  // 变为父目录
        auto it = dirCount_.find(cur);
        int v = (it == dirCount_.end() ? 0 : it->second) + delta;
        if (v <= 0) dirCount_.erase(cur);
        else dirCount_[cur] = v;
        if (cur.empty()) break;
    }
}

void RepoState::AddDirty(const std::wstring& rel, bool /*untracked*/) {
    std::wstring k = Key(rel);
    if (k.empty()) return;
    if (dirtyPaths_.insert(k).second) UpdateDirCounts(k, +1);
}

void RepoState::RemoveDirty(const std::wstring& rel) {
    std::wstring k = Key(rel);
    auto it = dirtyPaths_.find(k);
    if (it != dirtyPaths_.end()) {
        dirtyPaths_.erase(it);
        UpdateDirCounts(k, -1);
    }
}

void RepoState::Reset(const std::vector<GitDirtyPath>& dirty) {
    dirtyPaths_.clear();
    dirCount_.clear();
    for (auto& d : dirty) AddDirty(d.relpath, d.untracked);
}

void RepoState::ReplaceSubtree(const std::wstring& subtreeRel,
                               const std::vector<GitDirtyPath>& current) {
    std::wstring sub = Key(subtreeRel);
    std::vector<std::wstring> toRemove;
    for (auto& p : dirtyPaths_)
        if (IsPathPrefix(sub, p)) toRemove.push_back(p);

    std::unordered_set<std::wstring> newSet;
    for (auto& d : current) newSet.insert(Key(d.relpath));

    for (auto& p : toRemove)
        if (!newSet.count(p)) RemoveDirty(p);
    for (auto& d : current)
        if (!IsDirty(d.relpath)) AddDirty(d.relpath, d.untracked);
}

bool RepoState::IsDirty(const std::wstring& relpath) const {
    return dirtyPaths_.count(Key(relpath)) > 0;
}

StatusKind RepoState::StatusFor(const std::wstring& relpath) const {
    std::wstring k = Key(relpath);
    if (dirtyPaths_.count(k)) return StatusKind::Modified;
    auto it = dirCount_.find(k);
    if (it != dirCount_.end() && it->second > 0) return StatusKind::Modified;
    return StatusKind::Clean;
}

}  // namespace gs
