@echo off
rem Windows setup of FlightSim. The individual steps are in setup_windows.ps1.
rem Optional arguments are passed on, e.g.: setup_windows.bat -BuildDir D:\FlightSimBuild
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup_windows.ps1" %*
set EXIT_CODE=%ERRORLEVEL%
pause
exit /b %EXIT_CODE%
