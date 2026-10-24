@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0register-windows.ps1" -Unregister
if errorlevel 1 pause
