@echo off
call "%STAR_BUILD_VCVARS%" %1 >nul
if errorlevel 1 exit /b 1
set
