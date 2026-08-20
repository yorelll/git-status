// comtest.cpp —— COM 冒烟测试：验证覆盖 DLL 的 IShellIconOverlayIdentifier 三方法
// 直接 CoCreateInstance 两个 CLSID，对干净/脏/非仓库路径断言覆盖判定。
#include <windows.h>
#include <shlobj.h>
#include <initguid.h>
#include <fcntl.h>
#include <io.h>
#include <iostream>

DEFINE_GUID(CLSID_GitStatusClean, 0x0C310636, 0xD401, 0x43F1, 0x9F, 0x07,
            0xFE, 0x57, 0x8B, 0x72, 0xF5, 0xE9);
DEFINE_GUID(CLSID_GitStatusModified, 0xF9B12B78, 0x3178, 0x47F7, 0x98, 0x61,
            0xB6, 0x87, 0x5E, 0x14, 0x4E, 0xAE);

using std::wcout;
using std::wcerr;

static const wchar_t* Hr(HRESULT hr) {
    if (hr == S_OK) return L"S_OK";
    if (hr == S_FALSE) return L"S_FALSE";
    static wchar_t buf[32];
    swprintf(buf, 32, L"HRESULT 0x%08X", (unsigned)hr);
    return buf;
}

static int Check(const wchar_t* what, HRESULT got, HRESULT want) {
    bool ok = (got == want);
    wcout << L"  [ " << (ok ? L"PASS" : L"FAIL") << L" ] " << what
          << L": got " << Hr(got) << L", want " << Hr(want) << L"\n";
    return ok ? 0 : 1;
}

static int RunOverlay(const CLSID& clsid, const wchar_t* name,
                      const wchar_t* cleanPath, const wchar_t* dirtyPath,
                      const wchar_t* nonRepoPath, bool isCleanOverlay) {
    wcout << L"== " << name << L" ==\n";
    IShellIconOverlayIdentifier* p = nullptr;
    HRESULT hr = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IShellIconOverlayIdentifier, (void**)&p);
    if (FAILED(hr) || !p) {
        wcerr << L"CoCreateInstance 失败: 0x" << std::hex << (unsigned)hr << L"\n";
        return 1;
    }
    wchar_t iconFile[MAX_PATH * 2] = {0};
    int iconIdx = -1;
    DWORD flags = 0;
    hr = p->GetOverlayInfo(iconFile, MAX_PATH * 2, &iconIdx, &flags);
    wcout << L"  GetOverlayInfo -> " << Hr(hr) << L" file=" << iconFile
          << L" idx=" << iconIdx << L" flags=0x" << std::hex << flags << std::dec << L"\n";
    int fails = 0;
    // 图标索引必须是 0 基（0=干净 1=已修改），否则 shell 取不到图标 → 白图
    int wantIdx = isCleanOverlay ? 0 : 1;
    fails += Check(iconIdx == wantIdx ? L"图标索引(0基)" : L"图标索引(0基)", iconIdx == wantIdx ? S_OK : S_FALSE, S_OK);
    int prio = -1;
    p->GetPriority(&prio);
    wcout << L"  GetPriority -> " << prio << L"\n";
    HRESULT wantOk = isCleanOverlay ? S_OK : S_FALSE;
    HRESULT wantNo = isCleanOverlay ? S_FALSE : S_OK;
    fails += Check(L"干净路径", p->IsMemberOf(cleanPath, FILE_ATTRIBUTE_DIRECTORY), wantOk);
    fails += Check(L"脏路径", p->IsMemberOf(dirtyPath, FILE_ATTRIBUTE_DIRECTORY), wantNo);
    fails += Check(L"非仓库路径", p->IsMemberOf(nonRepoPath, FILE_ATTRIBUTE_DIRECTORY), S_FALSE);
    p->Release();
    return fails;
}

int wmain(int argc, wchar_t** argv) {
    _setmode(_fileno(stdout), _O_U8TEXT);
    if (argc < 4) {
        wcerr << L"用法: comtest.exe <干净路径> <脏路径> <非仓库路径>\n";
        return 1;
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int f = 0;
    f += RunOverlay(CLSID_GitStatusClean, L"Clean 覆盖", argv[1], argv[2], argv[3], true);
    f += RunOverlay(CLSID_GitStatusModified, L"Modified 覆盖", argv[1], argv[2], argv[3], false);
    CoUninitialize();
    wcout << L"== " << (f == 0 ? L"全部通过" : L"存在失败") << L" ==\n";
    return f == 0 ? 0 : 1;
}
