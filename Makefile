# GitStatus —— MinGW-w64 构建（无 CMake/MSVC，零运行时依赖）
SHELL = cmd.exe

CXX      = g++
WINDRES  = windres
CXXFLAGS = -std=c++20 -O2 -Wall -DUNICODE -D_UNICODE -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 -Isrc
LDFLAGS  = -static-libgcc -static-libstdc++ -static

BIN   = bin
BUILD = build

COMMON_SRC = src/common/ipc.cpp src/common/pathutil.cpp src/common/pipe_client.cpp
ENGINE_SRC = src/engine/git_status.cpp src/engine/repo_state.cpp
CACHE_SRC  = src/cache/main.cpp src/cache/cache_manager.cpp src/cache/dir_watcher.cpp src/cache/ipc_server.cpp
SHELL_SRC  = src/shell/dllmain.cpp src/shell/server.cpp src/shell/overlay.cpp src/shell/repo_lookup.cpp

HEADERS = src/common/status.h src/common/ipc.h src/common/pathutil.h src/common/pipe_client.h \
          src/engine/git_status.h src/engine/repo_state.h \
          src/cache/cache_manager.h src/cache/dir_watcher.h src/cache/ipc_server.h \
          src/shell/server.h src/shell/overlay.h src/shell/repo_lookup.h

LIBS_CACHE = -ladvapi32 -lshell32
LIBS_CLI   = -ladvapi32 -lshell32
LIBS_DLL   = -lole32 -ladvapi32 -lshell32 -luser32 -luuid

.PHONY: all clean install uninstall icons

all: $(BIN)/GitStatusCache.exe $(BIN)/GitStatusOverlay.dll $(BIN)/GitStatusCli.exe $(BIN)/GitStatusComTest.exe $(BIN)/setup.exe

$(BIN):
	@if not exist $(BIN) mkdir $(BIN)
$(BUILD):
	@if not exist $(BUILD) mkdir $(BUILD)

# ── 覆盖图标（由用户提供，非构建产物）────────────────
# 要求 icons/clean.ico 与 icons/modified.ico 存在（16px+32px 帧）。
# 顺序固定：clean 在前（图标索引 0）、modified 在后（索引 1）。
icons/clean.ico icons/modified.ico:
	@if not exist icons\clean.ico ( echo [ERROR] missing icons\clean.ico — put your own 16/32px .ico here. & exit /b 1 )
	@if not exist icons\modified.ico ( echo [ERROR] missing icons\modified.ico — put your own 16/32px .ico here. & exit /b 1 )

# ── 缓存进程 ─────────────────────────────────────────
$(BIN)/GitStatusCache.exe: $(COMMON_SRC) $(ENGINE_SRC) $(CACHE_SRC) $(HEADERS) | $(BIN)
	$(CXX) $(CXXFLAGS) -o $@ $(COMMON_SRC) $(ENGINE_SRC) $(CACHE_SRC) -mwindows $(LIBS_CACHE) $(LDFLAGS)

# ── 测试工具（控制台）────────────────────────────────
$(BIN)/GitStatusCli.exe: $(COMMON_SRC) $(ENGINE_SRC) tools/cli.cpp $(HEADERS) | $(BIN)
	$(CXX) $(CXXFLAGS) -municode -o $@ $(COMMON_SRC) $(ENGINE_SRC) tools/cli.cpp $(LIBS_CLI) $(LDFLAGS)

# ── COM 冒烟测试（需先注册 DLL）─────────────────────
$(BIN)/GitStatusComTest.exe: tools/comtest.cpp | $(BIN)
	$(CXX) $(CXXFLAGS) -municode -o $@ tools/comtest.cpp -lole32 -luuid $(LDFLAGS)

# ── 安装器 setup.exe（内嵌 DLL + cache.exe，单文件分发）─────
$(BUILD)/setup_res.o: tools/setup.rc tools/setup_ids.h bin/GitStatusOverlay.dll bin/GitStatusCache.exe | $(BUILD)
	$(WINDRES) -Itools -Ibin -O coff -o $@ tools/setup.rc

$(BIN)/setup.exe: tools/setup.cpp tools/setup_ids.h $(BUILD)/setup_res.o | $(BIN)
	$(CXX) $(CXXFLAGS) -municode -mwindows -o $@ tools/setup.cpp $(BUILD)/setup_res.o -lole32 -lshlwapi -lshell32 -ladvapi32 -luuid $(LDFLAGS)

# ── 覆盖图标 DLL ─────────────────────────────────────
$(BUILD)/overlay_res.o: src/shell/gitstatus_overlay.rc src/shell/resource.h icons/clean.ico icons/modified.ico | $(BUILD)
	$(WINDRES) -Isrc/shell -Iicons -O coff -o $@ src/shell/gitstatus_overlay.rc

$(BIN)/GitStatusOverlay.dll: $(COMMON_SRC) $(SHELL_SRC) $(HEADERS) $(BUILD)/overlay_res.o | $(BIN)
	$(CXX) $(CXXFLAGS) -shared -o $@ $(COMMON_SRC) $(SHELL_SRC) $(BUILD)/overlay_res.o $(LIBS_DLL) $(LDFLAGS) -Wl,--out-implib,$(BUILD)/liboverlay.a

# ── 安装 / 清理 ──────────────────────────────────────
install: all
	install.cmd

uninstall:
	uninstall.cmd

clean:
	@if exist $(BIN) rmdir /s /q $(BIN)
	@if exist $(BUILD) rmdir /s /q $(BUILD)
