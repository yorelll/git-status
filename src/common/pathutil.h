#pragma once
#include <string>

namespace gs {

// UTF-8 ↔ UTF-16（git porcelain 输出路径是 UTF-8 字节，Windows 路径是 UTF-16）
std::wstring Utf8ToWide(const std::string& utf8);
std::string  WideToUtf8(const std::wstring& wide);

// 反斜杠归一（git 输出用正斜杠，Windows 用反斜杠）
std::wstring Backslash(const std::wstring& p);

// 小写化（Windows 文件系统大小写不敏感，用作 map key 统一化）
std::wstring ToLowerW(const std::wstring& p);

// 去掉末尾反斜杠（保留盘根如 L"C:\\"）
std::wstring TrimTrailingSlash(const std::wstring& p);

// 父目录；盘根返回自身
std::wstring ParentDir(const std::wstring& p);

// 判断 <dir> 下是否存在 .git（目录或文件，文件=worktree/子模块）
bool HasDotGit(const std::wstring& dir);

// 绝对路径（GetFullPathNameW）
std::wstring AbsPath(const std::wstring& p);

bool IsDirectory(const std::wstring& p);
bool IsPathPrefix(const std::wstring& dir, const std::wstring& path);

}  // namespace gs
