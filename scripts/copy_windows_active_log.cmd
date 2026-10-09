@echo off
setlocal DisableDelayedExpansion
title OrcaSlicer Active Log Copy
set "ORCA_LOG_COPY_SCRIPT=%~dp0copy_windows_active_log.ps1"
set "ORCA_LOG_COPY_POWERSHELL=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if exist "%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe" set "ORCA_LOG_COPY_POWERSHELL=%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe"
if not exist "%ORCA_LOG_COPY_SCRIPT%" goto missing_script
if not exist "%ORCA_LOG_COPY_POWERSHELL%" goto missing_powershell
"%ORCA_LOG_COPY_POWERSHELL%" -NoProfile -STA -ExecutionPolicy Bypass -File "%ORCA_LOG_COPY_SCRIPT%"
set "ORCA_LOG_COPY_EXIT=%ERRORLEVEL%"
echo.
pause
exit /b %ORCA_LOG_COPY_EXIT%

:missing_script
echo Please extract the entire ZIP first and keep both script files together.
pause
exit /b 1

:missing_powershell
echo Windows PowerShell was not found on this computer.
pause
exit /b 1
