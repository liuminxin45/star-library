@echo off
where pwsh.exe >nul 2>nul
if errorlevel 1 goto windows_powershell
pwsh.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build.ps1" %*
exit /b %errorlevel%
:windows_powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build.ps1" %*
exit /b %errorlevel%
