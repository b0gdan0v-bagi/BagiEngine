# Setup Python virtual environment (PowerShell)

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$VenvDir = Join-Path (Split-Path -Parent $ScriptDir) ".venv"
$Requirements = Join-Path (Split-Path -Parent $ScriptDir) "requirements.txt"

if (-not (Test-Path (Join-Path $VenvDir "Scripts\python.exe"))) {
    Write-Host "[INFO] Creating virtual environment..." -ForegroundColor Cyan
    python -m venv $VenvDir
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[ERROR] Failed to create virtual environment" -ForegroundColor Red
        exit 1
    }
}

Write-Host "[INFO] Activating virtual environment..." -ForegroundColor Cyan
& (Join-Path $VenvDir "Scripts\Activate.ps1")

Write-Host "[INFO] Installing dependencies..." -ForegroundColor Cyan
pip install -q -r $Requirements
if ($LASTEXITCODE -ne 0) {
    Write-Host "[ERROR] Failed to install dependencies" -ForegroundColor Red
    exit 1
}

Write-Host "[OK] Virtual environment ready" -ForegroundColor Green
