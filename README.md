# GitStatus — 资源管理器 git 状态覆盖图标（小乌龟式）

轻量、零依赖的 Windows 资源管理器扩展：在 git 仓库内，**干净的目录/文件显示绿色圆圈 🟢，有改动的显示红色感叹号 🔴**（未跟踪文件同样视为脏），非仓库目录不显示任何图标。无需安装 TortoiseGit。

纯 **C++（MinGW-w64 g++）+ Win32** 编写，无 MSVC/CMake/vcpkg，无第三方运行时依赖，仅依赖系统已安装的 `git`。

---

## 构建

要求：MinGW-w64 g++（≥ C++20）与 `mingw32-make` 在 PATH 中，`git` 在 PATH 中。

```bat
mingw32-make
```

产物（`bin\`）：

| 文件 | 作用 |
|------|------|
| **`setup.exe`**       | **单文件安装器**（内嵌 DLL + 缓存进程，双击选目录安装/卸载，用户分发只需此文件） |
| `GitStatusOverlay.dll` | COM 覆盖图标扩展（Explorer 加载） |
| `GitStatusCache.exe`  | 后台缓存进程（维护各仓库状态聚合） |
| `GitStatusCli.exe`    | 测试/调试工具（见下） |
| `GitStatusComTest.exe`| COM 冒烟测试 |

**图标由用户提供**：把两个 `.ico` 文件放到 `icons\` 下即可，每个需含 16/32/48/256px 帧：

| 文件 | 含义 | 覆盖图标索引 |
|------|------|------|
| `icons\clean.ico` | 干净（默认左下角绿实心圆） | 0 |
| `icons\modified.ico` | 已修改（默认左下角红实心圆） | 1 |

顺序固定（`.rc` 中 clean 在前、modified 在后）；缺失时 `mingw32-make` 会报错提示。
仓库附带的 `icons\` 是两枚默认占位图标，可直接替换。

## 安装 / 卸载（用户）

```bat
setup.exe                          :: 双击：选安装目录 → UAC 提权 → 自动安装（写入程序与功能）
安装目录\uninstall.exe             :: 双击卸载；或在"控制面板→程序与功能"里卸载 GitStatus
setup.exe -install -silent -installdir "<目录>"   :: 静默安装（自动化）
安装目录\uninstall.exe -silent      :: 静默卸载
```

`setup.exe` 是自包含单文件：内嵌覆盖 DLL 与缓存进程，安装时释放到所选目录、
注册覆盖图标（HKLM+HKCU 双根）、写开机自启与"程序与功能"卸载项；卸载时精确删除
本工具全部注册表项（不影响 OneDrive 等其它覆盖软件）、停缓存、重启资源管理器、
删除安装目录。

## 安装 / 卸载（开发者就地，不经 setup.exe）

```bat
install.cmd    :: 提权（UAC）→ 就地注册 bin\ 下产物（HKLM+HKCU）→ 写开机自启 → 启动缓存 → 重启资源管理器
uninstall.cmd  :: 停缓存 → 注销 DLL（精确删除本工具的 CLSID 与覆盖标识，不影响 OneDrive 等）→ 删自启 → 删 bin\build 产物 → 重启资源管理器
```

> **需要管理员权限**：实测 Windows 11 上 Explorer 只加载注册在 `HKLM` 的覆盖图标，所以
> `install.cmd` 会请求 UAC 提权。注册后**必须重启资源管理器**（桌面会闪一下）才生效；脚本已自动处理。
> 升级 DLL 后同样需重启资源管理器。

## 使用

- 任何 `git` 仓库内的目录/文件自动出现覆盖图标；状态变化在约 1 秒内刷新。
- 仓库内的 `.git` 目录内部不显示图标。
- 删除仓库的 `.git` 目录后图标消失；新建仓库后自动出现（约 15 秒内）。

## 架构

```
GitStatusOverlay.dll               GitStatusCache.exe
┌──────────────────────────┐        ┌─────────────────────────────┐
│ IShellIconOverlayIdent   │ 管道    │ ipc_server 命名管道监听     │
│  Clean / Modified 两实例 │───────▶│ cache_manager 状态聚合表     │
│  repo_lookup 向上找 .git │        │ dir_watcher 每仓库一线程     │
│  pipe_client 100ms 超时  │◀───────│ git CLI 快照 + 增量更新      │
└──────────────────────────┘        └─────────────────────────────┘
```

- **状态检测**：初始用 `git status --porcelain -z --untracked-files=all` 做权威快照；随后 `ReadDirectoryChangesW` 递归监视仓库根，变更时对该路径子树跑一次 `git status`（pathspec 限定），增量更新"每目录脏计数"聚合。
- **绝不卡 UI**：`IsMemberOf` 仅做 80–100ms 的管道查询，超时则返回"无图标"；查询结果带 250ms 短缓存。
- **单实例缓存**：命名互斥体保证一个缓存进程；DLL 发现管道不通会自动拉起它。
- **注册**：CLSID 与覆盖标识同时写 `HKCU` 与 `HKLM`；Windows 11 上 Explorer 依赖 `HKLM` 加载，故安装需管理员。

## 测试 / 调试工具

```bat
bin\GitStatusCli.exe <repoRoot>        :: 快照扫描，打印脏路径 + 目录聚合状态
bin\GitStatusCli.exe --query <absPath> :: 经管道查询当前缓存中的状态
bin\GitStatusCli.exe --register <path> :: 通知缓存注册某仓库
bin\GitStatusCli.exe --poll <root> <path> <秒> :: 注册后每 0.5s 轮询
bin\GitStatusComTest.exe <干净> <脏> <非仓库>  :: COM 覆盖判定冒烟测试
```

状态码：`0=Clean(绿)`、`1=Modified(红)`、`255=NotRepo`、`254=Unknown`。

## 已知边界

- Windows 每进程最多显示 15 个覆盖图标；本工具注册名带前导空格 + 优先级 0，优先于 OneDrive/Dropbox 等，但被过多覆盖软件抢占时可能不显示。
- x64 资源管理器正常；个别 32 位进程（旧对话框）不加载 x64 DLL，无图标。
- 覆盖图标仅在资源管理器重启后重新加载（Explorer 进程级缓存）。
- 网络驱动器 / 超大仓库的性能未做专项优化（增量更新已事件驱动，空闲 CPU≈0）。
