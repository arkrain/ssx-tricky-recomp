@echo off
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0start-game.ps1" %*
if errorlevel 1 pause
