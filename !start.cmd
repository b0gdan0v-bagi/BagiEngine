@echo off
rem The double-click entry for CI\setup_venv.bat; arguments go on to it.
cd /d "%~dp0"
call CI\setup_venv.bat %*
set STATUS=%ERRORLEVEL%
pause
exit /b %STATUS%
