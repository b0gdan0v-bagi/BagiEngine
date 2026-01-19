@echo off
cd /d "%~dp0"

:: Setup meta-generator environment first
echo [INFO] Setting up Meta-Generator environment...
call CI\meta_generator\setup_env.bat
if %ERRORLEVEL% NEQ 0 (
    echo [WARNING] Meta-Generator setup had issues, continuing...
)

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

:: Run launcher using pythonw.exe (no console window)
if exist ".venv\Scripts\pythonw.exe" (
    echo [INFO] Starting BagiEngine Launcher...
    start "" ".venv\Scripts\pythonw.exe" -m CI.launcher.main
    echo [INFO] Launcher started. You can close this window.
    timeout /t 2 /nobreak >nul
) else (
    echo [ERROR] Virtual environment not found. Please run CI\setup_venv.bat first.
    pause
    exit /b 1
)
