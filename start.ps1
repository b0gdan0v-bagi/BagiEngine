# Script to open BagiEngine workspace in Cursor (Windows PowerShell)

Write-Host "Opening BagiEngine workspace in Cursor..." -ForegroundColor Green

$workspacePath = Join-Path $PSScriptRoot "BagiEngine.code-workspace"

# Try to find Cursor in common installation paths
$cursorPaths = @(
    "$env:LOCALAPPDATA\Programs\Cursor\Cursor.exe",
    "$env:PROGRAMFILES\Cursor\Cursor.exe",
    "$env:PROGRAMFILES(X86)\Cursor\Cursor.exe"
)

$cursorFound = $false

foreach ($path in $cursorPaths) {
    if (Test-Path $path) {
        Write-Host "Found Cursor at: $path" -ForegroundColor Cyan
        Start-Process -FilePath $path -ArgumentList $workspacePath
        $cursorFound = $true
        break
    }
}

if (-not $cursorFound) {
    # Try to use 'cursor' command if it's in PATH
    $cursorCommand = Get-Command cursor -ErrorAction SilentlyContinue
    if ($cursorCommand) {
        Write-Host "Using Cursor from PATH" -ForegroundColor Cyan
        & cursor $workspacePath
        $cursorFound = $true
    }
}

if (-not $cursorFound) {
    Write-Host "Cursor not found in standard paths, trying default application..." -ForegroundColor Yellow
    Start-Process $workspacePath
}

Write-Host ""
Write-Host "Workspace opened successfully!" -ForegroundColor Green
Write-Host "Press any key to continue..." -ForegroundColor Gray
$null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
