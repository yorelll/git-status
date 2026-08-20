@echo off
rem 开发者后备：直接就地注销（不经 uninstall.exe）。
rem 普通用户请用 安装目录\uninstall.exe。
setlocal
cd /d "%~dp0"

rem ---- HKLM keys need admin to delete ----
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo Requesting administrator privileges ^(UAC^)...
    powershell -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b 0
)

echo [1/6] Stop cache process...
taskkill /f /im GitStatusCache.exe >nul 2>&1

echo [2/6] Unregister overlay DLL ^(if present^)...
if exist bin\GitStatusOverlay.dll regsvr32 /s /u "%~dp0bin\GitStatusOverlay.dll"

rem ---- backup: explicit reg delete, works even if DLL was manually deleted ----
echo [3/6] Explicitly delete our registry keys ^(backup^)...
for %%R in (HKCU HKLM) do (
    reg delete "%%R\Software\Classes\CLSID\{0C310636-D401-43F1-9F07-FE578B72F5E9}" /f >nul 2>&1
    reg delete "%%R\Software\Classes\CLSID\{F9B12B78-3178-47F7-9861-B6875E144EAE}" /f >nul 2>&1
    reg delete "%%R\Software\Microsoft\Windows\CurrentVersion\Explorer\ShellIconOverlayIdentifiers\  GitStatusClean" /f >nul 2>&1
    reg delete "%%R\Software\Microsoft\Windows\CurrentVersion\Explorer\ShellIconOverlayIdentifiers\  GitStatusMod" /f >nul 2>&1
)

echo [4/6] Remove auto-start...
reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v GitStatusCache /f >nul 2>&1

echo [5/6] Stop Explorer and remove compiled artifacts...
powershell -NoProfile -Command "Stop-Process -Name explorer -Force -ErrorAction SilentlyContinue; Start-Sleep -Milliseconds 1500; Remove-Item -LiteralPath '%~dp0bin' -Recurse -Force -ErrorAction SilentlyContinue; Remove-Item -LiteralPath '%~dp0build' -Recurse -Force -ErrorAction SilentlyContinue"
if exist bin ( echo   [WARN] bin\ left over ^(maybe locked^). Delete manually: %~dp0bin )
if exist build ( echo   [WARN] build\ left over. Delete manually: %~dp0build )

echo [6/6] Restart Explorer...
start "" explorer.exe

echo.
echo GitStatus uninstalled.
exit /b 0
