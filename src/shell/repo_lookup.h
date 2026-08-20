#pragma once
#include <windows.h>
#include <mutex>
#include <string>
#include <unordered_map>

namespace gs {

// 向上逐级找 .git 确定仓库根；带 TTL 缓存（正负结果），避免每个条目都做磁盘 IO。
class RepoFinder {
public:
    static RepoFinder& Instance();

    // 返回仓库根绝对路径（无尾斜杠）；非仓库返回空串。
    std::wstring Find(const std::wstring& path);
    void Clear();

private:
    struct Entry { std::wstring root; DWORD ts; };
    static constexpr DWORD kTtlMs = 15000;
    std::mutex mtx_;
    std::unordered_map<std::wstring, Entry> cache_;  // key: 小写绝对路径
};

}  // namespace gs
