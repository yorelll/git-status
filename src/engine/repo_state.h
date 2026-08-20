#pragma once
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "git_status.h"
#include "../common/status.h"

namespace gs {

// 单个仓库的脏路径集合 + 每目录脏计数聚合。
// 目录状态 = 子树内任一脏路径 → Modified，否则 Clean（根目录 key = L""）。
class RepoState {
public:
    void Reset(const std::vector<GitDirtyPath>& dirty);
    // 用某子树当前的权威状态替换该子树记录（增量更新）
    void ReplaceSubtree(const std::wstring& subtreeRel, const std::vector<GitDirtyPath>& current);
    StatusKind StatusFor(const std::wstring& relpath) const;
    bool IsDirty(const std::wstring& relpath) const;
    size_t DirtyCount() const { return dirtyPaths_.size(); }

private:
    static std::wstring Key(const std::wstring& rel);
    void AddDirty(const std::wstring& rel, bool untracked);
    void RemoveDirty(const std::wstring& rel);
    void UpdateDirCounts(const std::wstring& rel, int delta);

    std::unordered_set<std::wstring> dirtyPaths_;    // 小写反斜杠 relpath
    std::unordered_map<std::wstring, int> dirCount_; // 小写目录 relpath → 脏计数（根 = ""）
};

}  // namespace gs
