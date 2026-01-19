# Helper script to create .env file without BOM
# Usage: .\create_env.ps1

$envFilePath = Join-Path $PSScriptRoot ".env"

if (Test-Path $envFilePath) {
    Write-Host "[WARNING] .env file already exists at: $envFilePath" -ForegroundColor Yellow
    $response = Read-Host "Do you want to overwrite it? (y/n)"
    if ($response -ne 'y' -and $response -ne 'Y') {
        Write-Host "Aborted." -ForegroundColor Red
        exit 0
    }
}

Write-Host "[INFO] Creating .env file..." -ForegroundColor Cyan

# Prompt for LLVM path
Write-Host ""
Write-Host "Enter the path to your LLVM installation directory:" -ForegroundColor Green
Write-Host "  Windows example: C:/Program Files/LLVM  or  D:/Tools/LLVM/bin" -ForegroundColor Gray
Write-Host "  macOS example: /usr/local/opt/llvm" -ForegroundColor Gray
Write-Host "  Linux example: /usr/lib/llvm-18" -ForegroundColor Gray
Write-Host ""
$llvmPath = Read-Host "LIBCLANG_PATH"

# Convert backslashes to forward slashes
$llvmPath = $llvmPath -replace '\\', '/'

# Create .env content
$content = @"
# BagiEngine Environment Configuration
# Path to LLVM installation directory (required for reflection code generation)
LIBCLANG_PATH=$llvmPath

"@

# Write file as UTF-8 without BOM
$utf8NoBom = New-Object System.Text.UTF8Encoding $false
[System.IO.File]::WriteAllText($envFilePath, $content, $utf8NoBom)

Write-Host ""
Write-Host "[SUCCESS] .env file created at: $envFilePath" -ForegroundColor Green
Write-Host "[INFO] LIBCLANG_PATH set to: $llvmPath" -ForegroundColor Cyan
Write-Host ""
Write-Host "You can now run the launcher with !start.cmd" -ForegroundColor Yellow
