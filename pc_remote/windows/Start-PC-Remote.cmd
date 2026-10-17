@echo off
setlocal
title PC Remote - Bluetooth + Tasks
cd /d "%~dp0"
python -u "%~dp0start_pc_remote.py"
if errorlevel 1 pause
endlocal
