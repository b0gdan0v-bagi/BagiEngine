#!/usr/bin/env bash
# Makes a checkout ready: the venv, what CI/ and tools/ need, the reflection generator's import, then the todo sign-in offer.
# Idempotent; no arguments needed. --no-login skips the sign-in offer. Exits non-zero when a step before the offer fails.
# The script never asks for, reads, prints or stores a token and never writes .env.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$SCRIPT_DIR")"
VENV_DIR="$ROOT/.venv"
VENV_PYTHON="$VENV_DIR/bin/python3"

LOGIN=1
for arg in "$@"; do
    case "$arg" in
        --no-login) LOGIN=0 ;;
        *)
            echo "[ERROR] unknown argument: $arg (only --no-login)"
            exit 1
            ;;
    esac
done

# 1. The venv. An existing one is left as it is: a worker's .venv is a symlink to the main checkout's.
if [ -x "$VENV_PYTHON" ]; then
    echo "[OK] venv: $VENV_DIR"
else
    echo "[INFO] Creating virtual environment..."
    if command -v uv > /dev/null 2>&1; then
        uv venv "$VENV_DIR" || { echo "[ERROR] Failed to create virtual environment"; exit 1; }
    elif command -v python3 > /dev/null 2>&1; then
        python3 -m venv "$VENV_DIR" || { echo "[ERROR] Failed to create virtual environment"; exit 1; }
    else
        echo "[ERROR] Neither uv nor python3 found. Install uv: curl -LsSf https://astral.sh/uv/install.sh | sh"
        exit 1
    fi
fi

# 2. The requirements of CI/ and tools/ in one call. uv gets the interpreter explicitly: without --python it installs
# into the VIRTUAL_ENV of the caller when one is active, not into .venv.
echo "[INFO] Installing dependencies..."
REQUIREMENTS=(-r "$ROOT/requirements.txt" -r "$ROOT/tools/control/requirements.txt")
if command -v uv > /dev/null 2>&1; then
    uv pip install --python "$VENV_PYTHON" "${REQUIREMENTS[@]}" || { echo "[ERROR] Failed to install dependencies"; exit 1; }
else
    "$VENV_PYTHON" -m pip install "${REQUIREMENTS[@]}" || { echo "[ERROR] Failed to install dependencies"; exit 1; }
fi

# 3. Configure runs the reflection generator, which needs only jinja2 (CMake picks this venv by itself).
JINJA_VERSION="$("$VENV_PYTHON" -c "import importlib.metadata as m, jinja2; print(m.version('jinja2'))")" || {
    echo "[ERROR] reflection generator: jinja2 does not import in $VENV_DIR"
    exit 1
}
echo "[OK] reflection generator: jinja2 $JINJA_VERSION"

# 4. The todo sign-in is an offer: a failure of it never fails the setup.
if [ "$LOGIN" -eq 0 ]; then
    echo "[OK] Virtual environment ready"
    exit 0
fi

LOGIN_FILE="${TODO_LOGIN_FILE:-${HOME:-}/.config/bagitodo/login.json}"
# Only the existence of the sign-in file and the key name in .env are tested: neither is opened for its value or printed.
if [ -e "$LOGIN_FILE" ] || [ -n "${TODO_API_URL:-}" ] || grep -q '^TODO_API_URL=.' "$ROOT/.env" 2> /dev/null; then
    echo "[OK] todo: signed in"
    echo "[OK] Virtual environment ready"
    exit 0
fi

TODO_PY="$ROOT/tools/todo/todo.py"
echo "[INFO] todo: not signed in. To sign in (the address comes from the owner):"
echo "       $VENV_PYTHON $TODO_PY login https://<todo address>"
if [ -t 0 ]; then
    read -r -p "Todo address (empty to skip): " TODO_ADDRESS
    if [ -n "$TODO_ADDRESS" ]; then
        "$VENV_PYTHON" "$TODO_PY" login "$TODO_ADDRESS" || echo "[WARN] todo: sign-in did not finish; run the command above to retry"
    fi
fi
echo "[OK] Virtual environment ready"
