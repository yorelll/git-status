// COM 进程内服务器：导出、类厂分发、注册/反注册（全 HKCU，免管理员）。
#include <windows.h>
#include <initguid.h>
#include "server.h"
#include "overlay.h"
#include "../common/pathutil.h"
#include <string>

using namespace gs;

// ── CLSID ────────────────────────────────────────────
DEFINE_GUID(CLSID_GitStatusClean, 0x0C310636, 0xD401, 0x43F1, 0x9F, 0x07,
            0xFE, 0x57, 0x8B, 0x72, 0xF5, 0xE9);
DEFINE_GUID(CLSID_GitStatusModified, 0xF9B12B78, 0x3178, 0x47F7, 0x98, 0x61,
            0xB6, 0x87, 0x5E, 0x14, 0x4E, 0xAE);

static const wchar_t kCleanGuidStr[] = L"{0C310636-D401-43F1-9F07-FE578B72F5E9}";
static const wchar_t kModGuidStr[] = L"{F9B12B78-3178-47F7-9861-B6875E144EAE}";

static HRESULT FactoryFor(OverlayKind kind, REFIID riid, void** ppv) {
    COverlayFactory* f = new COverlayFactory(kind);
    HRESULT hr = f->QueryInterface(riid, ppv);
    f->Release();
    return hr;
}

// ── 导出 ─────────────────────────────────────────────
extern "C" __declspec(dllexport) HRESULT WINAPI DllGetClassObject(REFCLSID rclsid,
                                                                  REFIID riid,
                                                                  LPVOID* ppv) {
    if (rclsid == CLSID_GitStatusClean) return FactoryFor(OverlayKind::Clean, riid, ppv);
    if (rclsid == CLSID_GitStatusModified) return FactoryFor(OverlayKind::Modified, riid, ppv);
    return CLASS_E_CLASSNOTAVAILABLE;
}

extern "C" __declspec(dllexport) HRESULT WINAPI DllCanUnloadNow() {
    return (g_moduleLocks == 0) ? S_OK : S_FALSE;
}

// ── 注册表助手 ───────────────────────────────────────
static void SetRegStr(HKEY root, const std::wstring& path, const wchar_t* valueName,
                      const std::wstring& value) {
    HKEY k = nullptr;
    if (RegCreateKeyExW(root, path.c_str(), 0, nullptr, 0, KEY_SET_VALUE,
                        nullptr, &k, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(k, valueName, 0, REG_SZ,
                       (const BYTE*)value.c_str(),
                       (DWORD)((value.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(k);
    }
}

static void DeleteRegTree(HKEY root, const std::wstring& path) {
    RegDeleteTreeW(root, path.c_str());  // Vista+ 可用；删不掉（无权限）也无妨
}

static void WriteClsidRoot(HKEY root, const wchar_t* guid, const wchar_t* friendly) {
    std::wstring base = L"Software\\Classes\\CLSID\\" + std::wstring(guid);
    SetRegStr(root, base, nullptr, friendly);
    wchar_t dllPath[MAX_PATH * 2] = {0};
    GetModuleFileNameW(g_hInst, dllPath, MAX_PATH * 2);
    SetRegStr(root, base + L"\\InprocServer32", nullptr, dllPath);
    SetRegStr(root, base + L"\\InprocServer32", L"ThreadingModel", L"Apartment");
}

static void WriteOverlayRoot(HKEY root, const wchar_t* regName, const wchar_t* guid) {
    std::wstring base = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\"
                        L"ShellIconOverlayIdentifiers\\" + std::wstring(regName);
    SetRegStr(root, base, nullptr, guid);
}

extern "C" __declspec(dllexport) HRESULT WINAPI DllRegisterServer() {
    // HKCU + HKLM 都写：实测 Windows 11 需 HKLM 才加载（需管理员），HKCU 作为兜底。
    // HKLM 写失败（非管理员）时忽略，不影响返回 S_OK。
    for (HKEY root : { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE }) {
        WriteClsidRoot(root, kCleanGuidStr, L"GitStatus Clean");
        WriteClsidRoot(root, kModGuidStr, L"GitStatus Modified");
        // 前导空格让键名按字母序排最前 → 更高覆盖图标优先级
        WriteOverlayRoot(root, L"  GitStatusClean", kCleanGuidStr);
        WriteOverlayRoot(root, L"  GitStatusMod", kModGuidStr);
    }
    return S_OK;
}

extern "C" __declspec(dllexport) HRESULT WINAPI DllUnregisterServer() {
    for (HKEY root : { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE }) {
        DeleteRegTree(root, L"Software\\Classes\\CLSID\\" + std::wstring(kCleanGuidStr));
        DeleteRegTree(root, L"Software\\Classes\\CLSID\\" + std::wstring(kModGuidStr));
        DeleteRegTree(root,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\"
                      L"ShellIconOverlayIdentifiers\\  GitStatusClean");
        DeleteRegTree(root,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\"
                      L"ShellIconOverlayIdentifiers\\  GitStatusMod");
    }
    return S_OK;
}
