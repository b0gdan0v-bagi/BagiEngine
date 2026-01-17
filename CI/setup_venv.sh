#!/bin/bash
# Setup Python virtual environment (macOS/Linux)

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VENV_DIR="$SCRIPT_DIR/../.venv"
REQUIREMENTS="$SCRIPT_DIR/../requirements.txt"

if [ ! -f "$VENV_DIR/bin/python" ]; then
    echo "[INFO] Creating virtual environment..."
    python3 -m venv "$VENV_DIR"
    if [ $? -ne 0 ]; then
        echo "[ERROR] Failed to create virtual environment"
        exit 1
    fi
fi

echo "[INFO] Activating virtual environment..."
source "$VENV_DIR/bin/activate"

echo "[INFO] Installing dependencies..."
pip install -q -r "$REQUIREMENTS"
if [ $? -ne 0 ]; then
    echo "[ERROR] Failed to install dependencies"
    exit 1
fi

echo "[OK] Virtual environment ready"
