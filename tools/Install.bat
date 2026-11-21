@echo off
rem Double-click to install the Ring Chief mod (AlphaRing) into Halo MCC on Steam.
rem Runs install-ringchief.ps1 from this folder; Windows' "downloaded script" block doesn't apply.
rem Extra arguments are passed on, e.g.  Install.bat -McCPath "D:\Games\Halo The Master Chief Collection"
title Ring Chief mod installer
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install-ringchief.ps1" %*
set RC_CODE=%ERRORLEVEL%
if not "%RC_CODE%"=="0" (
  echo.
  echo Not installed. If it said "Copy failed", right-click Install.bat and choose "Run as administrator".
)
echo.
if not defined RC_NOPAUSE pause
exit /b %RC_CODE%
