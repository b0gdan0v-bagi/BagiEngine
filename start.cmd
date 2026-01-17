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

:: Run launcher in a separate window using venv Python directly
if exist ".venv\Scripts\python.exe" (
    start "BagiEngine Launcher" ".venv\Scripts\python.exe" -m CI.launcher.main
) else (
    echo [ERROR] Virtual environment not found. Please run CI\setup_venv.bat first.
    pause
    exit /b 1
)
