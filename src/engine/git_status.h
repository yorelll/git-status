#pragma once
#include <string>
#include <vector>

namespace gs {

// 一条脏路径（相对仓库根，反斜杠分隔，未转小写）
struct GitDirtyPath {
    std::wstring relpath;
    bool untracked;  // ?? 未跟踪（两态下同样算脏）
};

// 快照 / 单路径扫描：
//   git --literal-pathspecs -c core.quotePath=true -C <root> status --porcelain -z --untracked-files=all [-- <pathspec...>]
// pathspec 为空 = 全仓库；给定则限定子树（增量更新用）。
// 成功返回 true 并填充 out；失败返回 false 并给出 errMsg。
bool GitStatusScan(const std::wstring& repoRoot,
                   const std::vector<std::wstring>& pathspec,
                   std::vector<GitDirtyPath>& out,
                   std::wstring& errMsg);

}  // namespace gs
