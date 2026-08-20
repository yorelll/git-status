#pragma once
#include <cstdint>
#include <string>

namespace gs {

// 命名管道协议 —— 唯一共享协议头，cache.exe 与 overlay.dll 都必须按此编解码。
//
// 请求帧（长度前缀，小端）：
//   [u32 type][u32 pathLen 字节数][path UTF-16LE 字节，含末尾 \0]
// 响应帧：
//   [i8 status]  StatusKind 的值（Clean/Modified/NotRepo/Unknown）
constexpr uint32_t kIpcQueryStatus   = 0x01;  // path → status
constexpr uint32_t kIpcRegisterRepo  = 0x02;  // path = 仓库根目录 → 0 表示已受理

// 单路径上限：4K 个 wchar 含 \0
constexpr uint32_t kMaxPathBytes = 4096 * 2;

struct RequestHeader {
    uint32_t type;
    uint32_t pathLen;  // 字节数
};

// 管道名按用户名隔离，避免多用户同机冲突。
std::wstring PipeName();

// 缓存进程单实例互斥体名（Local\GitStatusCache_<user>）。
std::wstring InstanceMutexName();

}  // namespace gs
