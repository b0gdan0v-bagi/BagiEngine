@echo off
cd /d "%~dp0"

:: Setup venv if needed
if not exist ".venv\Scripts\python.exe" (
    echo [INFO] Setting up Python virtual environment...
    call CI\setup_venv.bat
    if %ERRORLEVEL% NEQ 0 (
        echo [ERROR] Failed to setup virtual environment
        pause
        exit /b 1
    )
)

:: Activate and run launcher
call .venv\Scripts\activate.bat
python -m CI.launcher.main
