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

:: Check Python
python --version >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Python not found. Please install Python 3.10+
    echo Download: https://www.python.org/downloads/
    goto :error
)
echo [OK] Python found

:: Check/Create venv if not exists
if not exist "%VENV_DIR%\Scripts\python.exe" (
    echo.
    echo [INFO] Virtual environment not found. Creating...
    python -m venv "%VENV_DIR%"
    if errorlevel 1 (
        echo [ERROR] Failed to create virtual environment
        goto :error
    )
    echo [OK] Virtual environment created
)

:: Activate venv
call "%VENV_DIR%\Scripts\activate.bat"

:: Check/Install pip packages
echo.
echo Installing Python dependencies...
pip install clang pyqt6 jinja2 --quiet
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
