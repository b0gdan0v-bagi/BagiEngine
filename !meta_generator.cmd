@echo off
cd /d "%~dp0"

:: Setup meta-generator environment first
echo [INFO] Setting up Meta-Generator environment...
call CI\meta_generator\setup_env.bat
if %ERRORLEVEL% NEQ 0 (
    echo [WARNING] Meta-Generator setup had issues, continuing anyway...
)

:: Run Meta-Generator GUI
echo.
echo [INFO] Starting Meta-Generator GUI...
if exist ".venv\Scripts\python.exe" (
    ".venv\Scripts\python.exe" CI\meta_generator\meta_generator_gui.py
) else (
    echo [ERROR] Virtual environment not found. Please run !start.cmd first.
    pause
    exit /b 1
)
