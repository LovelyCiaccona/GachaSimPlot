@echo off
setlocal
set "ROOT=%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%ROOT%一键停止抽卡模拟器.ps1"
if errorlevel 1 pause
endlocal
