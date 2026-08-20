// GitStatusCli.exe —— 测试/调试工具（无 UI，直接调用引擎逻辑 + 管道冒烟）。
//
//   GitStatusCli <repoRoot>                快照扫描：打印脏路径 + 根/一级目录聚合状态
//   GitStatusCli --query <absPath>         通过命名管道查询运行中的缓存
//   GitStatusCli --register <absPath>      通知缓存注册某仓库根
//   GitStatusCli --poll <root> <path> <秒>  注册后每 500ms 轮询查询
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <iostream>
#include <algorithm>
#include <unordered_map>
#include "../src/common/ipc.h"
#include "../src/common/pathutil.h"
#include "../src/common/pipe_client.h"
#include "../src/common/status.h"
#include "../src/engine/git_status.h"
#include "../src/engine/repo_state.h"

using namespace gs;

static const wchar_t* Describe(StatusKind st) {
    switch (st) {
        case StatusKind::Clean: return L"Clean(绿)";
        case StatusKind::Modified: return L"Modified(红)";
        case StatusKind::NotRepo: return L"NotRepo";
        default: return L"Unknown";
    }
}

static void Usage() {
    std::wcout << L"GitStatusCli 用法：\n"
                  L"  GitStatusCli <repoRoot>\n"
                  L"  GitStatusCli --query <absPath>\n"
                  L"  GitStatusCli --register <absPath>\n"
                  L"  GitStatusCli --poll <root> <path> <秒>\n";
}

static int DoSnapshot(const std::wstring& root) {
    std::vector<GitDirtyPath> dirty;
    std::wstring err;
    if (!GitStatusScan(root, {}, dirty, err)) {
        std::wcerr << L"扫描失败：" << err << L"\n";
        return 1;
    }
    RepoState state;
    state.Reset(dirty);
    std::wcout << L"== 快照（" << root << L"）脏路径 " << dirty.size() << L" 条 ==\n";
    for (auto& d : dirty)
        std::wcout << (d.untracked ? L"?? " : L"   ") << d.relpath << L"\n";

    std::wcout << L"== 聚合状态 ==\n";
    std::wcout << L"  根目录: " << Describe(state.StatusFor(L"")) << L"\n";
    std::unordered_map<std::wstring, StatusKind> dirs;
    for (auto& d : dirty) {
        size_t p = d.relpath.find(L'\\');
        std::wstring top = (p == std::wstring::npos) ? d.relpath : d.relpath.substr(0, p);
        if (!top.empty()) dirs[top] = state.StatusFor(top);
    }
    std::vector<std::wstring> keys;
    for (auto& [k, v] : dirs) keys.push_back(k);
    std::sort(keys.begin(), keys.end());
    for (auto& k : keys) std::wcout << L"  " << k << L": " << Describe(dirs[k]) << L"\n";
    return 0;
}

static int DoQuery(const std::wstring& path) {
    StatusKind st;
    if (!PipeQuery(path, kIpcQueryStatus, st, true)) {
        std::wcerr << L"查询失败（缓存未运行？）\n";
        return 1;
    }
    std::wcout << Describe(st) << L" (" << (int)st << L")\n";
    return 0;
}

static int DoRegister(const std::wstring& path) {
    StatusKind st;
    if (!PipeQuery(path, kIpcRegisterRepo, st, true)) {
        std::wcerr << L"注册失败（缓存未运行？）\n";
        return 1;
    }
    std::wcout << L"已通知缓存注册：" << path << L"\n";
    return 0;
}

static int DoPoll(const std::wstring& root, const std::wstring& path, int seconds) {
    StatusKind st;
    if (!PipeQuery(root, kIpcRegisterRepo, st, true)) {
        std::wcerr << L"注册失败（缓存未运行？）\n";
        return 1;
    }
    for (int i = 0; i < seconds * 2; ++i) {
        if (PipeQuery(path, kIpcQueryStatus, st, false)) {
            wchar_t ts[64];
            swprintf(ts, 64, L"[%.1fs]", i * 0.5);
            std::wcout << ts << L" " << Describe(st) << L"\n";
        } else {
            std::wcout << L"查询失败\n";
        }
        Sleep(500);
    }
    return 0;
}

int wmain(int argc, wchar_t** argv) {
    // 宽字符输出转 UTF-8，避免 C locale 下中文被吞
    _setmode(_fileno(stdout), _O_U8TEXT);
    _setmode(_fileno(stderr), _O_U8TEXT);
    SetSelfModule(GetModuleHandleW(nullptr));
    if (argc < 2) { Usage(); return 1; }
    std::wstring cmd = argv[1];
    if (cmd == L"--query" && argc >= 3) return DoQuery(argv[2]);
    if (cmd == L"--register" && argc >= 3) return DoRegister(argv[2]);
    if (cmd == L"--poll" && argc >= 5) {
        int secs = _wtoi(argv[4]);
        return DoPoll(argv[2], argv[3], secs > 0 ? secs : 10);
    }
    if (cmd[0] != L'-') return DoSnapshot(cmd);
    Usage();
    return 1;
}
