#include "git_status.h"
#include "../common/pathutil.h"
#include <windows.h>
#include <vector>

namespace gs {

// ── 命令行 ──────────────────────────────────────────────

static std::wstring QuoteArg(const std::wstring& s) {
    if (s.find_first_of(L" \t\"") == std::wstring::npos) return s;
    std::wstring r = L"\"";
    for (wchar_t c : s) {
        if (c == L'"') r += L"\\\"";
        else r += c;
    }
    r += L'"';
    return r;
}

static std::wstring BuildStatusCmd(const std::wstring& repoRoot,
                                   const std::vector<std::wstring>& pathspec) {
    // --no-optional-locks 至关重要：裸 git status 会创建/删除 .git/index.lock 并回写
    // .git/index，而这些文件事件又被 DirWatcher 捕获 → 触发再次全量重扫 → 自反馈死循环。
    std::wstring cmd = L"git --no-optional-locks --literal-pathspecs -c core.quotePath=true -C " +
                       QuoteArg(repoRoot) + L" status --porcelain -z --untracked-files=all";
    if (!pathspec.empty()) {
        cmd += L" --";
        for (auto& ps : pathspec) cmd += L" " + QuoteArg(ps);
    }
    return cmd;
}

// 运行 git 并捕获 stdout（带 5s 读超时防挂死）。退出码非 0 → false。
static bool RunGit(const std::wstring& cmdLine, std::string& outBytes, std::wstring& errMsg) {
    HANDLE hRead = nullptr, hWrite = nullptr;
    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) { errMsg = L"CreatePipe 失败"; return false; }
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{ sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> cmd(cmdLine.begin(), cmdLine.end());
    cmd.push_back(0);
    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(hWrite);
    if (!ok) {
        CloseHandle(hRead);
        errMsg = L"无法启动 git（错误码 " + std::to_wstring(GetLastError()) + L"）";
        return false;
    }

    // CreatePipe 返回的是同步句柄（无 FILE_FLAG_OVERLAPPED）：对它传 OVERLAPPED 属 API
    // 未定义行为，且同步 ReadFile 会无限阻塞、使超时彻底失效。改为 PeekNamedPipe 轮询，
    // 双闸超时：
    //   - 空闲 30s：读到数据即重置。注意 porcelain 输出走 stdio 全缓冲，大仓库可能
    //     整个扫描期零字节输出、结束才一次吐完——所以不能只靠空闲判定"活着"。
    //   - 总 90s：容纳 monorepo/-uall 的合法慢扫描（10~60s 常见），兼防病态无限占用。
    constexpr DWORD kIdleTimeoutMs = 30000;
    constexpr DWORD kTotalTimeoutMs = 90000;
    const DWORD startTick = GetTickCount();
    DWORD lastDataTick = startTick;
    std::string out;
    bool ioFail = false;
    for (;;) {
        DWORD avail = 0;
        if (!PeekNamedPipe(hRead, nullptr, 0, nullptr, &avail, nullptr)) {
            break;  // 写端已关（ERROR_BROKEN_PIPE）= git 正常结束；其它错误也按 EOF 处理
        }
        if (avail > 0) {
            char buf[8192];
            DWORD n = 0;
            if (!ReadFile(hRead, buf, sizeof(buf), &n, nullptr) || n == 0) break;
            out.append(buf, n);
            lastDataTick = GetTickCount();  // 有数据流动 → git 活着
            continue;
        }
        DWORD now = GetTickCount();  // 减法比较，回转安全
        if (now - lastDataTick > kIdleTimeoutMs || now - startTick > kTotalTimeoutMs) {
            TerminateProcess(pi.hProcess, 1);  // 真挂死/病态 → 杀掉避免泄漏
            ioFail = true;
            break;
        }
        Sleep(20);  // 轮询间隔：响应性无差别，CPU 更省
    }
    CloseHandle(hRead);

    WaitForSingleObject(pi.hProcess, 5000);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (ioFail || exitCode != 0) {
        errMsg = L"git 退出码 " + std::to_wstring(exitCode) + L"：" + Utf8ToWide(out);
        return false;
    }
    outBytes = std::move(out);
    return true;
}

// ── 解析 porcelain -z ──────────────────────────────────

// 还原 git 对非常规路径的 C 引用（\" \\ \ooo）
static std::string UnquotePath(const std::string& tok) {
    if (tok.empty() || tok[0] != '"') return tok;
    std::string r;
    size_t i = 1;
    while (i < tok.size() && tok[i] != '"') {
        if (tok[i] == '\\' && i + 1 < tok.size()) {
            char c = tok[i + 1];
            if (c == '"' || c == '\\') {
                r += c;
                i += 2;
            } else if (c >= '0' && c <= '7') {
                int v = 0, cnt = 0;
                size_t j = i + 1;
                while (j < tok.size() && cnt < 3 && tok[j] >= '0' && tok[j] <= '7') {
                    v = v * 8 + (tok[j] - '0');
                    j++;
                    cnt++;
                }
                r += (char)v;
                i = j;
            } else {
                r += tok[i];
                i++;
            }
        } else {
            r += tok[i];
            i++;
        }
    }
    return r;
}

static void ParsePorcelainZ(const std::string& data, std::vector<GitDirtyPath>& out) {
    // 按 NUL 切分
    std::vector<std::string> toks;
    size_t i = 0;
    while (i < data.size()) {
        size_t nul = data.find('\0', i);
        if (nul == std::string::npos) {
            toks.push_back(data.substr(i));
            break;
        }
        toks.push_back(data.substr(i, nul - i));
        i = nul + 1;
    }
    for (size_t t = 0; t < toks.size(); ++t) {
        const std::string& tok = toks[t];
        if (tok.size() < 3 || tok[2] != ' ') continue;   // 形如 "XY <path>"
        char x = tok[0], y = tok[1];
        if (x == ' ' && y == ' ') continue;
        std::string bytes = UnquotePath(tok.substr(3));
        if (bytes.empty()) continue;
        GitDirtyPath p;
        p.relpath = Backslash(Utf8ToWide(bytes));
        p.untracked = (x == '?' || y == '?');
        out.push_back(p);
        // 重命名/复制：-z 模式下源名后还跟着一条裸目标字段
        if ((x == 'R' || x == 'C') && t + 1 < toks.size()) {
            std::string dst = UnquotePath(toks[t + 1]);
            if (!dst.empty()) {
                GitDirtyPath q;
                q.relpath = Backslash(Utf8ToWide(dst));
                q.untracked = false;
                out.push_back(q);
            }
            ++t;
        }
    }
}

bool GitStatusScan(const std::wstring& repoRoot,
                   const std::vector<std::wstring>& pathspec,
                   std::vector<GitDirtyPath>& out,
                   std::wstring& errMsg) {
    std::wstring cmd = BuildStatusCmd(repoRoot, pathspec);
    std::string bytes;
    if (!RunGit(cmd, bytes, errMsg)) return false;
    ParsePorcelainZ(bytes, out);
    return true;
}

}  // namespace gs
