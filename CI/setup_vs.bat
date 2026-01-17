@echo off
setlocal
set BUILD_DIR=build

:: Возвращаемся в корень из папки скрипта, если нужно
cd /d "%~dp0\.."

:: Если папка build уже есть, мы её не удаляем (для скорости), 
:: но CMake обновит конфиг.
if not exist %BUILD_DIR% (
    mkdir %BUILD_DIR%
)

echo [INFO] Starting CMake configuration...
cd %BUILD_DIR%

:: Запускаем генерацию проекта для Visual Studio.
:: -A x64 принудительно ставит 64-битную сборку.
cmake .. -A x64

if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] CMake failed!
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo [SUCCESS] Project generated in /%BUILD_DIR%
echo [INFO] Open the .sln file in Visual Studio to start coding.
pause