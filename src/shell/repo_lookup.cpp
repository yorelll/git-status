#include "repo_lookup.h"
#include "../common/pathutil.h"

namespace gs {

RepoFinder& RepoFinder::Instance() {
    static RepoFinder inst;
    return inst;
}

void RepoFinder::Clear() {
    std::lock_guard g(mtx_);
    cache_.clear();
}

std::wstring RepoFinder::Find(const std::wstring& path) {
    std::wstring abs = TrimTrailingSlash(Backslash(AbsPath(path)));
    if (abs.size() < 3 || abs[1] != L':') return L"";

    std::wstring key = ToLowerW(abs);
    DWORD now = GetTickCount();
    {
        std::lock_guard g(mtx_);
        auto it = cache_.find(key);
        if (it != cache_.end() && now - it->second.ts < kTtlMs) return it->second.root;
    }

    std::wstring result;
    std::wstring dir = abs;
    DWORD att = GetFileAttributesW(dir.c_str());
    if (att != INVALID_FILE_ATTRIBUTES && !(att & FILE_ATTRIBUTE_DIRECTORY)) {
        dir = ParentDir(dir);  // 文件：从其所在目录开始找
    }
    for (;;) {
        if (HasDotGit(dir)) { result = dir; break; }
        std::wstring parent = ParentDir(dir);
        if (parent == dir) break;  // 已到盘根
        dir = parent;
    }

    {
        std::lock_guard g(mtx_);
        if (cache_.size() > 4096) cache_.clear();
        cache_[key] = { result, now };
    }
    return result;
}

}  // namespace gs
