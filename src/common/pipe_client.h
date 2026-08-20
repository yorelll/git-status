#pragma once
#include <windows.h>
#include <string>
#include "status.h"

namespace gs {

// 设置"当前模块句柄"，用于定位同目录下的 GitStatusCache.exe。
// DLL 在 DllMain 调用；EXE（测试工具）在 main 里传 GetModuleHandleW(nullptr)。
void SetSelfModule(HMODULE h);

// 向缓存进程发起一次同步请求（QueryStatus / RegisterRepo）。
// ensureRunning：管道不通时尝试拉起缓存进程（再重试一次）。
// 成功返回 true；失败（超时/未运行）返回 false。RegisterRepo 时 outStatus 无意义。
bool PipeQuery(const std::wstring& path, uint32_t type,
               StatusKind& outStatus, bool ensureRunning);

}  // namespace gs
