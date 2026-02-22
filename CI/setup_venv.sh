#!/bin/bash
# Setup Python virtual environment (macOS/Linux) using uv

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
VENV_DIR="$REPO_ROOT/.venv"
REQUIREMENTS="$REPO_ROOT/requirements.txt"

if ! command -v uv &> /dev/null; then
    echo "[ERROR] uv not found. Install: curl -LsSf https://astral.sh/uv/install.sh | sh"
    exit 1
fi

if [ ! -f "$VENV_DIR/bin/python" ]; then
    echo "[INFO] Creating virtual environment..."
    uv venv "$VENV_DIR"
    if [ $? -ne 0 ]; then
        echo "[ERROR] Failed to create virtual environment"
        exit 1
    fi
fi

echo "[INFO] Installing dependencies..."
cd "$REPO_ROOT"
uv pip install -r "$REQUIREMENTS"
if [ $? -ne 0 ]; then
    echo "[ERROR] Failed to install dependencies"
    exit 1
fi

echo "[OK] Virtual environment ready"
