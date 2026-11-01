@echo off
powershell.exe -NoProfile -File "%~dp0mirage.ps1" gui
if errorlevel 1 pause
