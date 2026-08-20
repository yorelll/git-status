#pragma once
#include <cstdint>

namespace gs {

// 目录/文件覆盖图标状态（两态起步）。
// 值即 IPC 响应帧中传输的字节。
enum class StatusKind : uint8_t {
    Clean = 0,      // 干净 → 绿色圆圈
    Modified = 1,   // 有改动 → 红色感叹号
    NotRepo = 0xFF, // 不在任何已注册仓库内
    Unknown = 0xFE, // 缓存未就绪 / 查询失败（调用方应显示无图标）
};

}  // namespace gs
