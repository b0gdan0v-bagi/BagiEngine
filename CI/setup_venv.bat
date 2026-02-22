@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "REPO_ROOT=%SCRIPT_DIR%.."
set "VENV_DIR=%REPO_ROOT%\.venv"
set "REQUIREMENTS=%REPO_ROOT%\requirements.txt"

where uv >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] uv not found. Install: powershell -c "irm https://astral.sh/uv/install.ps1 | iex"
    exit /b 1
)

if not exist "%VENV_DIR%\Scripts\python.exe" (
    echo [INFO] Creating virtual environment...
    uv venv "%VENV_DIR%"
    if %ERRORLEVEL% NEQ 0 (
        echo [ERROR] Failed to create virtual environment
        exit /b 1
    )
)

echo [INFO] Installing dependencies...
cd /d "%REPO_ROOT%"
uv pip install -r "%REQUIREMENTS%"
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Failed to install dependencies
    exit /b 1
)

echo [OK] Virtual environment ready
endlocal
