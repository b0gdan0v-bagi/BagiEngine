@echo off
rem Wrapper of setup_venv.ps1 for a double click and for a game's own .bat; takes --no-login (or -NoLogin) to skip the todo sign-in offer.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup_venv.ps1" %*
exit /b %ERRORLEVEL%
