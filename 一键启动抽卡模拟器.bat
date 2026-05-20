@echo off
setlocal
cd /d " "%%~dp0
powershell.exe -NoProfile -ExecutionPolicy Bypass -File %%~dp0一键启动抽卡模拟器.ps1
if errorlevel 1 pause
endlocal
