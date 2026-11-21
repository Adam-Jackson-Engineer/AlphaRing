@echo off
rem Double-click to remove the Ring Chief mod from Halo MCC (puts back any DLL it replaced).
title Ring Chief mod uninstaller
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install-ringchief.ps1" -Uninstall %*
set RC_CODE=%ERRORLEVEL%
if not "%RC_CODE%"=="0" (
  echo.
  echo Not removed. Close Halo MCC, or right-click Uninstall.bat and choose "Run as administrator".
)
echo.
if not defined RC_NOPAUSE pause
exit /b %RC_CODE%
