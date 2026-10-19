@echo off
setlocal DisableDelayedExpansion
title OrcaSlicer GUI Diagnostic
set "ORCA_DIAGNOSTIC_SCRIPT=%~dp0capture_windows_gui_state.ps1"
set "ORCA_DIAGNOSTIC_POWERSHELL=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if exist "%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe" set "ORCA_DIAGNOSTIC_POWERSHELL=%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe"
if not exist "%ORCA_DIAGNOSTIC_SCRIPT%" goto missing_script
if not exist "%ORCA_DIAGNOSTIC_POWERSHELL%" goto missing_powershell
"%ORCA_DIAGNOSTIC_POWERSHELL%" -NoProfile -ExecutionPolicy Bypass -File "%ORCA_DIAGNOSTIC_SCRIPT%" -Interactive -OpenOutputFolder
set "ORCA_DIAGNOSTIC_EXIT=%ERRORLEVEL%"
if not "%ORCA_DIAGNOSTIC_EXIT%"=="0" echo Capture failed. Please keep the error text above.
echo.
pause
exit /b %ORCA_DIAGNOSTIC_EXIT%

:missing_script
echo Please extract the entire ZIP first. Keep this launcher beside capture_windows_gui_state.ps1.
pause
exit /b 1

:missing_powershell
echo Windows PowerShell was not found on this computer.
pause
exit /b 1
