# Setup Python virtual environment (PowerShell) using uv

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptDir
$VenvDir = Join-Path $RepoRoot ".venv"
$Requirements = Join-Path $RepoRoot "requirements.txt"

if (-not (Get-Command uv -ErrorAction SilentlyContinue)) {
    Write-Host "[ERROR] uv not found. Install: irm https://astral.sh/uv/install.ps1 | iex" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path (Join-Path $VenvDir "Scripts\python.exe"))) {
    Write-Host "[INFO] Creating virtual environment..." -ForegroundColor Cyan
    uv venv $VenvDir
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[ERROR] Failed to create virtual environment" -ForegroundColor Red
        exit 1
    }
}

Write-Host "[INFO] Installing dependencies..." -ForegroundColor Cyan
Set-Location $RepoRoot
uv pip install -r $Requirements
if ($LASTEXITCODE -ne 0) {
    Write-Host "[ERROR] Failed to install dependencies" -ForegroundColor Red
    exit 1
}

Write-Host "[OK] Virtual environment ready" -ForegroundColor Green
