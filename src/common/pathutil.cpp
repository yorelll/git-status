#include "pathutil.h"
#include <windows.h>
#include <algorithm>

namespace gs {

std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), nullptr, 0);
    if (len <= 0) return L"";
    std::wstring w(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), w.data(), len);
    return w;
}

std::string WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wide.data(), (int)wide.size(), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    std::string s(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), (int)wide.size(), s.data(), len, nullptr, nullptr);
    return s;
}

std::wstring Backslash(const std::wstring& p) {
    std::wstring r = p;
    std::replace(r.begin(), r.end(), L'/', L'\\');
    return r;
}

std::wstring ToLowerW(const std::wstring& p) {
    std::wstring r = p;
    std::transform(r.begin(), r.end(), r.begin(), [](wchar_t c) { return (wchar_t)towlower(c); });
    return r;
}

std::wstring TrimTrailingSlash(const std::wstring& p) {
    std::wstring r = p;
    while (r.size() > 3 && (r.back() == L'\\' || r.back() == L'/')) r.pop_back();
    return r;
}

std::wstring ParentDir(const std::wstring& p) {
    std::wstring r = TrimTrailingSlash(p);
    size_t pos = r.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return r;
    std::wstring parent = r.substr(0, pos);
    if (parent.empty()) parent = L"\\";
    // 盘根（如 C:）补回反斜杠
    if (parent.size() == 2 && parent[1] == L':') parent += L'\\';
    return parent;
}

bool HasDotGit(const std::wstring& dir) {
    std::wstring d = TrimTrailingSlash(dir);
    std::wstring g = d + L"\\.git";
    DWORD att = GetFileAttributesW(g.c_str());
    return att != INVALID_FILE_ATTRIBUTES;
}

std::wstring AbsPath(const std::wstring& p) {
    wchar_t buf[MAX_PATH * 2] = {0};
    DWORD len = GetFullPathNameW(p.c_str(), MAX_PATH * 2, buf, nullptr);
    if (len == 0 || len >= MAX_PATH * 2) return p;
    return std::wstring(buf);
}

bool IsDirectory(const std::wstring& p) {
    DWORD att = GetFileAttributesW(p.c_str());
    if (att == INVALID_FILE_ATTRIBUTES) return false;
    return (att & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool IsPathPrefix(const std::wstring& dir, const std::wstring& path) {
    // dir 是 path 的祖先（或相等）。双方应已统一大小写/斜杠。
    if (dir.empty()) return true;
    if (path.size() < dir.size()) return false;
    if (path.size() == dir.size()) return path == dir;
    if (path.compare(0, dir.size(), dir) != 0) return false;
    // 边界：dir 之后必须是分隔符
    return path[dir.size()] == L'\\' || path[dir.size()] == L'/';
}

}  // namespace gs
