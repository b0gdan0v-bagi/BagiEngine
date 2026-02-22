@echo off
setlocal enabledelayedexpansion

echo ============================================
echo BagiEngine Meta-Generator Environment Setup
echo ============================================
echo.

:: Get script directory
set "SCRIPT_DIR=%~dp0"
set "REPO_ROOT=%SCRIPT_DIR%..\.."
set "VENV_DIR=%REPO_ROOT%\.venv"
set "REQUIREMENTS=%SCRIPT_DIR%requirements.txt"

:: Check uv
where uv >nul 2>&1
if errorlevel 1 (
    echo [ERROR] uv not found. Install: powershell -c "irm https://astral.sh/uv/install.ps1 | iex"
    goto :error
)
echo [OK] uv found

:: Check/Create venv if not exists
if not exist "%VENV_DIR%\Scripts\python.exe" (
    echo.
    echo [INFO] Virtual environment not found. Creating...
    uv venv "%VENV_DIR%"
    if errorlevel 1 (
        echo [ERROR] Failed to create virtual environment
        goto :error
    )
    echo [OK] Virtual environment created
)

:: Install pip packages from requirements.txt
echo.
echo Installing Python dependencies...
cd /d "%REPO_ROOT%"
uv pip install -r "%REQUIREMENTS%"
if errorlevel 1 (
    echo [ERROR] Failed to install Python packages
    goto :error
)
echo [OK] Python packages installed

:: Check LLVM
echo.
echo Checking LLVM installation...

if defined LIBCLANG_PATH (
    if exist "!LIBCLANG_PATH!\libclang.dll" (
        echo [OK] LLVM found via LIBCLANG_PATH: !LIBCLANG_PATH!
        goto :success
    )
)

:: Check common paths (checking VS versions first, then standalone)
set "LLVM_PATH="

:: Visual Studio 2022 LLVM (check Professional, Enterprise, Community in order)
if not defined LLVM_PATH if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Tools\Llvm\bin\libclang.dll" (
    set "LLVM_PATH=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Tools\Llvm\bin"
)
if not defined LLVM_PATH if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Tools\Llvm\bin\libclang.dll" (
    set "LLVM_PATH=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Tools\Llvm\bin"
)
if not defined LLVM_PATH if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\bin\libclang.dll" (
    set "LLVM_PATH=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\bin"
)

:: Visual Studio 2019 LLVM
if not defined LLVM_PATH if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Tools\Llvm\bin\libclang.dll" (
    set "LLVM_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Tools\Llvm\bin"
)
if not defined LLVM_PATH if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise\VC\Tools\Llvm\bin\libclang.dll" (
    set "LLVM_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise\VC\Tools\Llvm\bin"
)
if not defined LLVM_PATH if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Tools\Llvm\bin\libclang.dll" (
    set "LLVM_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Tools\Llvm\bin"
)

:: Standalone LLVM installations (fallback)
if not defined LLVM_PATH if exist "C:\Program Files\LLVM\bin\libclang.dll" (
    set "LLVM_PATH=C:\Program Files\LLVM\bin"
)
if not defined LLVM_PATH if exist "C:\Program Files (x86)\LLVM\bin\libclang.dll" (
    set "LLVM_PATH=C:\Program Files (x86)\LLVM\bin"
)
if not defined LLVM_PATH if exist "C:\LLVM\bin\libclang.dll" (
    set "LLVM_PATH=C:\LLVM\bin"
)

if defined LLVM_PATH (
    echo [OK] LLVM found at: !LLVM_PATH!
    echo.
    echo Recommendation: Set LIBCLANG_PATH environment variable:
    echo   setx LIBCLANG_PATH "!LLVM_PATH!"
    goto :success
)

echo [WARNING] LLVM/libclang not found automatically.
echo.
echo LLVM is REQUIRED for Meta-Generator. Please install LLVM:
echo   1. Download from: https://github.com/llvm/llvm-project/releases
echo   2. Run installer, select "Add LLVM to system PATH"
echo   3. Or set LIBCLANG_PATH to LLVM bin directory
echo.
echo After installation, run this script again or use GUI Settings.
goto :end

:success
echo.
echo ============================================
echo Setup complete! You can now use meta_generator.
echo.
echo   CLI: python %SCRIPT_DIR%meta_generator.py --help
echo   GUI: python %SCRIPT_DIR%meta_generator_gui.py
echo ============================================
goto :end

:error
echo.
echo Setup failed. Please fix the errors above.
exit /b 1

:end
endlocal
