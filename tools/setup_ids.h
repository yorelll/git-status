#pragma once

// setup.exe 内嵌的负载资源 ID
// install.rc 顺序声明，故资源索引固定：
//   0 = GitStatusOverlay.dll
//   1 = GitStatusCache.exe
#define PAYLOAD_DLL    101   // RCDATA
#define PAYLOAD_CACHE  102   // RCDATA

// 本工具在 HKLM 卸载项里的注册表键名 / 显示名
#define UNINSTALL_KEY  L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\GitStatus"
#define APP_DISPLAY    L"GitStatus"
