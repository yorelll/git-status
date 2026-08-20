@echo off
rem 开发者后备：直接就地构建产物注册（不经 setup.exe）。
rem 普通用户请用 bin\setup.exe。
setlocal
cd /d "%~dp0"

if not exist bin\GitStatusOverlay.dll (
    echo [ERROR] bin\GitStatusOverlay.dll not found. Run: mingw32-make
    exit /b 1
)

rem ---- Overlay icons require HKLM registration (admin) ----
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo Requesting administrator privileges ^(UAC^)...
    powershell -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b 0
)

echo [1/4] Register overlay DLL (HKLM + HKCU)...
regsvr32 /s "%~dp0bin\GitStatusOverlay.dll"

echo [2/4] Set cache auto-start...
reg add "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v GitStatusCache /t REG_SZ /d "\"%~dp0bin\GitStatusCache.exe\" -autorun" /f >nul

echo [3/4] Start cache process...
start "" "%~dp0bin\GitStatusCache.exe"

echo [4/4] Restart Explorer (desktop will blink; required to reload overlays)...
taskkill /f /im explorer.exe >nul 2>&1
start explorer.exe

echo.
echo GitStatus installed.
