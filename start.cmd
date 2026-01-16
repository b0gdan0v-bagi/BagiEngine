@echo off
REM Script to open BagiEngine workspace in Cursor (Windows CMD)

echo Opening BagiEngine workspace in Cursor...

REM Try to find Cursor in common installation paths
if exist "%LOCALAPPDATA%\Programs\Cursor\Cursor.exe" (
    "%LOCALAPPDATA%\Programs\Cursor\Cursor.exe" "%~dp0BagiEngine.code-workspace"
    goto :end
)

if exist "%PROGRAMFILES%\Cursor\Cursor.exe" (
    "%PROGRAMFILES%\Cursor\Cursor.exe" "%~dp0BagiEngine.code-workspace"
    goto :end
)

REM If Cursor is in PATH, use it directly
where cursor >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    cursor "%~dp0BagiEngine.code-workspace"
    goto :end
)

REM Fallback: try to open with default application
echo Cursor not found in standard paths, trying default application...
start "" "%~dp0BagiEngine.code-workspace"
goto :end

:end
echo.
echo Workspace opened successfully!
pause
