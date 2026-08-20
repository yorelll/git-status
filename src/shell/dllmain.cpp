#include <windows.h>
#include "server.h"
#include "../common/pipe_client.h"

HINSTANCE g_hInst = nullptr;

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            g_hInst = hinst;
            gs::SetSelfModule(hinst);  // 用于定位同目录 GitStatusCache.exe
            DisableThreadLibraryCalls(hinst);
            break;
        case DLL_PROCESS_DETACH:
        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
            break;
    }
    return TRUE;
}
