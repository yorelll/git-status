#include "ipc.h"
#include <windows.h>

namespace gs {

static std::wstring UserName() {
    wchar_t user[256] = {0};
    DWORD n = sizeof(user) / sizeof(user[0]);
    GetUserNameW(user, &n);
    return std::wstring(user);
}

std::wstring PipeName() {
    static std::wstring name = L"\\\\.\\pipe\\GitStatusCache_" + UserName();
    return name;
}

std::wstring InstanceMutexName() {
    static std::wstring m = L"Local\\GitStatusCache_" + UserName();
    return m;
}

}  // namespace gs
