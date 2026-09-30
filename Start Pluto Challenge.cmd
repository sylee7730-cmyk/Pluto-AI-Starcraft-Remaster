@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\start-pluto.ps1" -Multiplayer -Challenge %*
set "PLUTO_EXIT=%ERRORLEVEL%"
if not "%PLUTO_EXIT%"=="0" pause
exit /b %PLUTO_EXIT%
