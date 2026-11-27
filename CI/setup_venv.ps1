# Makes a checkout ready (Windows): the venv, what CI/ and tools/ need, the reflection generator's import, then the todo
# sign-in offer. Idempotent; no arguments needed. -NoLogin or --no-login skips the sign-in offer. Exits non-zero when a
# step before the offer fails. The script never asks for, reads, prints or stores a token and never writes .env.
# No param block: PowerShell would not bind the "--no-login" the README and the .sh show to a switch.

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$Root = Split-Path -Parent $ScriptDir
$VenvDir = Join-Path $Root ".venv"
$VenvPython = Join-Path $VenvDir "Scripts\python.exe"
$HasUv = [bool](Get-Command uv -ErrorAction SilentlyContinue)

function Fail($Message) {
    Write-Host "[ERROR] $Message" -ForegroundColor Red
    exit 1
}

$NoLogin = $false
foreach ($Arg in $args) {
    if ($Arg -eq "-NoLogin" -or $Arg -eq "--no-login") { $NoLogin = $true }
    else { Fail "unknown argument: $Arg (only --no-login)" }
}

# 1. The venv. An existing one is left as it is.
if (Test-Path $VenvPython) {
    Write-Host "[OK] venv: $VenvDir" -ForegroundColor Green
} else {
    Write-Host "[INFO] Creating virtual environment..." -ForegroundColor Cyan
    if ($HasUv) {
        uv venv $VenvDir
    } elseif (Get-Command py -ErrorAction SilentlyContinue) {
        py -3 -m venv $VenvDir
    } elseif (Get-Command python -ErrorAction SilentlyContinue) {
        python -m venv $VenvDir
    } else {
        Fail "Neither uv nor Python found. Install uv: irm https://astral.sh/uv/install.ps1 | iex"
    }
    if ($LASTEXITCODE -ne 0) { Fail "Failed to create virtual environment" }
}

# 2. The requirements of CI/ and tools/ in one call. uv gets the interpreter explicitly: without --python it installs
# into the VIRTUAL_ENV of the caller when one is active, not into .venv.
Write-Host "[INFO] Installing dependencies..." -ForegroundColor Cyan
$Requirements = @("-r", (Join-Path $Root "requirements.txt"), "-r", (Join-Path $Root "tools\control\requirements.txt"))
if ($HasUv) {
    uv pip install --python $VenvPython @Requirements
} else {
    & $VenvPython -m pip install @Requirements
}
if ($LASTEXITCODE -ne 0) { Fail "Failed to install dependencies" }

# 3. Configure runs the reflection generator, which needs only jinja2 (CMake picks this venv by itself).
$JinjaVersion = & $VenvPython -c "import importlib.metadata as m, jinja2; print(m.version('jinja2'))"
if ($LASTEXITCODE -ne 0) { Fail "reflection generator: jinja2 does not import in $VenvDir" }
Write-Host "[OK] reflection generator: jinja2 $JinjaVersion" -ForegroundColor Green

# 4. The todo sign-in is an offer: a failure of it never fails the setup.
if (-not $NoLogin) {
    $HomeDir = if ($env:USERPROFILE) { $env:USERPROFILE } else { $HOME }
    $LoginFile = if ($env:TODO_LOGIN_FILE) { $env:TODO_LOGIN_FILE } else { Join-Path $HomeDir ".config\bagitodo\login.json" }
    # Only the existence of the sign-in file and the key name in .env are tested: neither is printed.
    # In a game this checkout is its engine/ submodule, and the game root's .env counts too.
    $EnvDirs = @($Root)
    if (Get-Command git -ErrorAction SilentlyContinue) {
        $Superproject = git -C $Root rev-parse --show-superproject-working-tree 2>$null
        if ($Superproject) { $EnvDirs += $Superproject }
    }
    $HasEnvUrl = $false
    foreach ($EnvDir in $EnvDirs) {
        $EnvFile = Join-Path $EnvDir ".env"
        if ((Test-Path $EnvFile) -and (Select-String -Path $EnvFile -Pattern '^TODO_API_URL=.' -Quiet)) { $HasEnvUrl = $true }
    }
    $SignedIn = (Test-Path $LoginFile) -or $env:TODO_API_URL -or $HasEnvUrl
    if ($SignedIn) {
        Write-Host "[OK] todo: signed in" -ForegroundColor Green
    } else {
        $TodoPy = Join-Path $Root "tools\todo\todo.py"
        Write-Host "[INFO] todo: not signed in. To sign in (the address comes from the owner):" -ForegroundColor Cyan
        Write-Host "       $VenvPython $TodoPy login https://<todo address>"
        if (-not [Console]::IsInputRedirected) {
            $Address = Read-Host "Todo address (empty to skip)"
            if ($Address) {
                & $VenvPython $TodoPy login $Address
                if ($LASTEXITCODE -ne 0) { Write-Host "[WARN] todo: sign-in did not finish; run the command above to retry" -ForegroundColor Yellow }
            }
        }
    }
}

Write-Host "[OK] Virtual environment ready" -ForegroundColor Green
