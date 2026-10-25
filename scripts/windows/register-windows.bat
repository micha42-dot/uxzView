@echo off
rem Beside uxzView.exe this works without editing. Or enter its full path here:
set "IXVIEW_EXE=%~dp0uxzView.exe"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0register-windows.ps1" -ExecutablePath "%IXVIEW_EXE%" -OpenSettings
if errorlevel 1 pause
