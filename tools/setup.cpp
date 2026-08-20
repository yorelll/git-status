// setup.exe —— 单文件安装/卸载器
//   作为 setup.exe 运行：选目录 → 提权 → 释放负载 → 注册 → 写卸载项 → 启动
//   作为 uninstall.exe 运行：提权 → 停进程 → 注销 → 删文件 → 删卸载项 → 重启 explorer
// 负载（DLL + cache.exe）以 RCDATA 资源内嵌，故 setup.exe 是单文件分发包。
#include <windows.h>
#include <tlhelp32.h>
#include <shlobj.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <objbase.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "setup_ids.h"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")

// ── CLSID（与 src/shell/server.cpp 完全一致）────────
static const wchar_t* kCleanGuid = L"{0C310636-D401-43F1-9F07-FE578B72F5E9}";
static const wchar_t* kModGuid   = L"{F9B12B78-3178-47F7-9861-B6875E144EAE}";

// ── 通用助手 ───────────────────────────────────────
static void SetRegStr(HKEY root, const std::wstring& path, const wchar_t* name,
                      const std::wstring& value) {
    HKEY k = nullptr;
    if (RegCreateKeyExW(root, path.c_str(), 0, nullptr, 0, KEY_SET_VALUE,
                        nullptr, &k, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(k, name, 0, REG_SZ, (const BYTE*)value.c_str(),
                       (DWORD)((value.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(k);
    }
}
static void SetRegExpandStr(HKEY root, const std::wstring& path, const wchar_t* name,
                            const std::wstring& value) {
    HKEY k = nullptr;
    if (RegCreateKeyExW(root, path.c_str(), 0, nullptr, 0, KEY_SET_VALUE,
                        nullptr, &k, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(k, name, 0, REG_EXPAND_SZ, (const BYTE*)value.c_str(),
                       (DWORD)((value.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(k);
    }
}
static void DelRegTree(HKEY root, const std::wstring& path) {
    RegDeleteTreeW(root, path.c_str());
}

static bool IsAdmin() {
    BOOL admin = FALSE;
    SID_IDENTIFIER_AUTHORITY auth = SECURITY_NT_AUTHORITY;
    PSID sid = nullptr;
    if (AllocateAndInitializeSid(&auth, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                  DOMAIN_ALIAS_RID_ADMINS, 0,0,0,0,0,0, &sid)) {
        CheckTokenMembership(nullptr, sid, &admin);
        FreeSid(sid);
    }
    return admin != FALSE;
}

// 自提权：用 -rerun 标记重新拉起自己，避免 UAC 后参数丢失
static bool RelaunchAsAdmin(const std::wstring& exePath, const std::wstring& arg) {
    SHELLEXECUTEINFOW sei{ sizeof(sei) };
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = exePath.c_str();
    std::wstring params = arg;
    sei.lpParameters = params.c_str();
    sei.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei) || !sei.hProcess) return false;
    WaitForSingleObject(sei.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(sei.hProcess, &code);
    CloseHandle(sei.hProcess);
    return code == 0;
}

static bool ExtractResource(int resId, const std::wstring& outPath) {
    HRSRC h = FindResourceW(nullptr, MAKEINTRESOURCEW(resId), RT_RCDATA);
    if (!h) return false;
    HGLOBAL g = LoadResource(nullptr, h);
    if (!g) return false;
    DWORD size = SizeofResource(nullptr, h);
    void* data = LockResource(g);
    if (!data || !size) return false;
    HANDLE f = CreateFileW(outPath.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = WriteFile(f, data, size, &written, nullptr);
    CloseHandle(f);
    return ok && written == size;
}

// 终止同名进程
static void StopProcess(const wchar_t* exeName) {
    PROCESSENTRY32W pe{ sizeof(pe) };
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, exeName) == 0) {
                HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                if (h) { TerminateProcess(h, 0); CloseHandle(h); }
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
}

// 停止并等待 explorer 退出（释放 DLL 句柄）
static void StopExplorer() {
    StopProcess(L"explorer.exe");
    // 等最多 3 秒让 explorer 退出，期间清图标缓存
    for (int i = 0; i < 30; ++i) {
        if (!FindWindowW(L"Shell_TrayWnd", nullptr)) break;
        Sleep(100);
    }
    wchar_t path[MAX_PATH] = {0};
    if (GetTempPathW(MAX_PATH, path)) {
        std::wstring cache = std::wstring(path) + L"iconcache_*";
        WIN32_FIND_DATAW fd{};
        HANDLE f = FindFirstFileW(cache.c_str(), &fd);
        if (f != INVALID_HANDLE_VALUE) {
            do { DeleteFileW((std::wstring(path) + fd.cFileName).c_str()); } while (FindNextFileW(f, &fd));
            FindClose(f);
        }
    }
}

// 卸载程序不能删正在运行的自己 → 用 cmd 排一个延迟：等自身退出后，
// 删除 uninstall.exe，再删整个安装目录。用 ping 做延迟（timeout 无 stdin 时不可靠）。
static void ScheduleSelfDelete(const std::wstring& exePath) {
    std::wstring dir = exePath;
    size_t p = dir.find_last_of(L"\\/");
    if (p != std::wstring::npos) dir.resize(p);
    std::wstring cmd = L"/c ping 127.0.0.1 -n 3 >nul & del /f /q \"" + exePath + L"\" & rd /s /q \"" + dir + L"\"";
    STARTUPINFOW si{ sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    CreateProcessW(L"C:\\Windows\\System32\\cmd.exe", buf.data(), nullptr, nullptr,
                   FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (pi.hProcess) CloseHandle(pi.hProcess);
    if (pi.hThread) CloseHandle(pi.hThread);
}

// ── 注册 / 注销（HKLM + HKCU 双根，与 DllRegisterServer 等价）──────
static void RegisterOverlay(const std::wstring& dllPath) {
    for (HKEY root : { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE }) {
        SetRegStr(root, L"Software\\Classes\\CLSID\\" + std::wstring(kCleanGuid), nullptr, L"GitStatus Clean");
        SetRegStr(root, L"Software\\Classes\\CLSID\\" + std::wstring(kCleanGuid) + L"\\InprocServer32", nullptr, dllPath);
        SetRegStr(root, L"Software\\Classes\\CLSID\\" + std::wstring(kCleanGuid) + L"\\InprocServer32", L"ThreadingModel", L"Apartment");
        SetRegStr(root, L"Software\\Classes\\CLSID\\" + std::wstring(kModGuid), nullptr, L"GitStatus Modified");
        SetRegStr(root, L"Software\\Classes\\CLSID\\" + std::wstring(kModGuid) + L"\\InprocServer32", nullptr, dllPath);
        SetRegStr(root, L"Software\\Classes\\CLSID\\" + std::wstring(kModGuid) + L"\\InprocServer32", L"ThreadingModel", L"Apartment");
        // 前导空格让覆盖标识按字母序排最前 → 更高优先级
        SetRegStr(root, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\ShellIconOverlayIdentifiers\\  GitStatusClean", nullptr, kCleanGuid);
        SetRegStr(root, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\ShellIconOverlayIdentifiers\\  GitStatusMod", nullptr, kModGuid);
    }
}

static void UnregisterOverlay() {
    for (HKEY root : { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE }) {
        DelRegTree(root, L"Software\\Classes\\CLSID\\" + std::wstring(kCleanGuid));
        DelRegTree(root, L"Software\\Classes\\CLSID\\" + std::wstring(kModGuid));
        DelRegTree(root, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\ShellIconOverlayIdentifiers\\  GitStatusClean");
        DelRegTree(root, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\ShellIconOverlayIdentifiers\\  GitStatusMod");
    }
}

// ── 浏览选目录 ─────────────────────────────────────
static std::wstring BrowseFolder(HWND owner) {
    std::wstring result;
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dlg)))) return result;
    dlg->SetOptions(FOS_PICKFOLDERS);
    // 默认建议 Program Files\GitStatus
    PWSTR defPath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramFiles, 0, nullptr, &defPath)) && defPath) {
        IShellItem* def = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(defPath, nullptr, IID_PPV_ARGS(&def)))) {
            dlg->SetFolder(def);
            def->Release();
        }
        CoTaskMemFree(defPath);
    }
    if (SUCCEEDED(dlg->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                result = path;
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dlg->Release();
    return result;
}

// ── 安装流程 ───────────────────────────────────────
static int DoInstall(const std::wstring& exePath, std::wstring dir, bool silent) {
    if (dir.empty()) {
        // 1. 选目录
        dir = BrowseFolder(nullptr);
        if (dir.empty()) { MessageBoxW(nullptr, L"未选择安装目录，已取消。", APP_DISPLAY, MB_OK | MB_ICONINFORMATION); return 1; }
    }

    // 2. 确保目录存在
    CreateDirectoryW(dir.c_str(), nullptr);

    // 3. 释放负载
    std::wstring dllPath = dir + L"\\GitStatusOverlay.dll";
    std::wstring cachePath = dir + L"\\GitStatusCache.exe";
    std::wstring uninstallPath = dir + L"\\uninstall.exe";
    if (!ExtractResource(PAYLOAD_DLL, dllPath)) { if(!silent)MessageBoxW(nullptr, L"释放 DLL 失败。", APP_DISPLAY, MB_OK | MB_ICONERROR); return 2; }
    if (!ExtractResource(PAYLOAD_CACHE, cachePath)) { if(!silent)MessageBoxW(nullptr, L"释放缓存进程失败。", APP_DISPLAY, MB_OK | MB_ICONERROR); return 2; }
    // 4. 复制自身为 uninstall.exe
    if (!CopyFileW(exePath.c_str(), uninstallPath.c_str(), FALSE)) { if(!silent)MessageBoxW(nullptr, L"创建卸载程序失败。", APP_DISPLAY, MB_OK | MB_ICONERROR); return 2; }

    // 5. 注册覆盖图标
    RegisterOverlay(dllPath);

    // 6. 开机自启缓存进程
    SetRegExpandStr(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", L"GitStatusCache", L"\"" + cachePath + L"\" -autorun");

    // 7. 写 HKLM 卸载项（控制面板"程序与功能"里可见）
    SetRegStr(HKEY_LOCAL_MACHINE, UNINSTALL_KEY, L"DisplayName", APP_DISPLAY);
    SetRegStr(HKEY_LOCAL_MACHINE, UNINSTALL_KEY, L"DisplayVersion", L"1.0");
    SetRegStr(HKEY_LOCAL_MACHINE, UNINSTALL_KEY, L"Publisher", L"GitStatus");
    SetRegStr(HKEY_LOCAL_MACHINE, UNINSTALL_KEY, L"DisplayIcon", dllPath);
    SetRegStr(HKEY_LOCAL_MACHINE, UNINSTALL_KEY, L"InstallLocation", dir);
    SetRegStr(HKEY_LOCAL_MACHINE, UNINSTALL_KEY, L"UninstallString", L"\"" + uninstallPath + L"\"");
    SetRegStr(HKEY_LOCAL_MACHINE, UNINSTALL_KEY, L"QuietUninstallString", L"\"" + uninstallPath + L"\" -silent");

    // 8. 启动缓存进程
    ShellExecuteW(nullptr, L"open", cachePath.c_str(), nullptr, nullptr, SW_HIDE);

    // 9. 刷新 explorer（覆盖图标需重载）
    if (silent) {
        // 静默安装不自动重启 explorer（供自动化/命令行场景）
    } else {
        std::wstring msg = L"安装完成。\n\n安装目录：" + dir + L"\n\n点击确定重启资源管理器使覆盖图标生效（桌面会闪一下）。";
        if (MessageBoxW(nullptr, msg.c_str(), APP_DISPLAY, MB_OKCANCEL | MB_ICONINFORMATION) == IDOK) {
            StopExplorer();
            ShellExecuteW(nullptr, L"open", L"explorer.exe", nullptr, nullptr, SW_SHOWNORMAL);
        }
    }
    return 0;
}

// ── 卸载流程 ───────────────────────────────────────
static int DoUninstall(const std::wstring& exePath, bool silent) {
    // exePath 即 uninstall.exe，安装目录 = 其所在目录
    std::wstring dir = exePath;
    size_t p = dir.find_last_of(L"\\/");
    if (p != std::wstring::npos) dir.resize(p);
    std::wstring dllPath = dir + L"\\GitStatusOverlay.dll";
    std::wstring cachePath = dir + L"\\GitStatusCache.exe";

    // 1. 停缓存进程
    StopProcess(L"GitStatusCache.exe");

    // 2. 注销覆盖图标（显式 reg delete，不依赖 DLL 是否还在）
    UnregisterOverlay();

    // 3. 删开机自启
    {
        HKEY k = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER,
                          L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                          0, KEY_SET_VALUE, &k) == ERROR_SUCCESS) {
            RegDeleteValueW(k, L"GitStatusCache");
            RegCloseKey(k);
        }
    }

    // 4. 删卸载项
    DelRegTree(HKEY_LOCAL_MACHINE, UNINSTALL_KEY);

    // 5. 停 explorer 释放 DLL 句柄，再删文件
    StopExplorer();
    DeleteFileW(dllPath.c_str());
    DeleteFileW(cachePath.c_str());
    // uninstall.exe 自己不能删正在运行的自己 → 调度重启后删除
    ScheduleSelfDelete(exePath);

    // 6. 重启 explorer
    ShellExecuteW(nullptr, L"open", L"explorer.exe", nullptr, nullptr, SW_SHOWNORMAL);

    if (!silent) {
        MessageBoxW(nullptr, L"卸载完成。", APP_DISPLAY, MB_OK | MB_ICONINFORMATION);
    }
    return 0;
}

// ── 入口 ───────────────────────────────────────────
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR cmdLine, int show) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    wchar_t exePath[MAX_PATH * 2] = {0};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH * 2);
    std::wstring exe(exePath);
    std::wstring name = exe;
    size_t p = name.find_last_of(L"\\/");
    if (p != std::wstring::npos) name = name.substr(p + 1);

    std::wstring args(cmdLine ? cmdLine : L"");
    bool silent = args.find(L"-silent") != std::wstring::npos;

    // 按文件名判定：uninstall.exe → 卸载；否则 → 安装
    bool isUninstaller = (_wcsicmp(name.c_str(), L"uninstall.exe") == 0) ||
                        (args.find(L"-uninstall") != std::wstring::npos);

    // 解析 -installdir <dir>（静默/自动化安装用）
    std::wstring installDir;
    size_t dpos = args.find(L"-installdir");
    if (dpos != std::wstring::npos) {
        dpos += wcslen(L"-installdir");
        while (dpos < args.size() && (args[dpos] == L' ' || args[dpos] == L'\t')) ++dpos;
        size_t dstart = dpos;
        if (dpos < args.size() && args[dpos] == L'"') {
            ++dpos;
            size_t dend = args.find(L'"', dpos);
            installDir = args.substr(dpos, dend == std::wstring::npos ? std::wstring::npos : dend - dpos);
        } else {
            while (dpos < args.size() && args[dpos] != L' ' && args[dpos] != L'\t') ++dpos;
            installDir = args.substr(dstart, dpos - dstart);
        }
    }

    // 必须管理员
    if (!IsAdmin()) {
        std::wstring param;
        if (isUninstaller) param = silent ? L"-uninstall -silent" : L"-uninstall";
        else param = silent ? L"-install -silent -installdir \"" + installDir + L"\"" : L"-install";
        bool ok = RelaunchAsAdmin(exe, param);
        CoUninitialize();
        return ok ? 0 : 1;
    }

    int ret;
    if (isUninstaller) {
        ret = DoUninstall(exe, silent);
    } else {
        ret = DoInstall(exe, installDir, silent);
    }
    CoUninitialize();
    return ret;
}
