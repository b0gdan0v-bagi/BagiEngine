#!/bin/bash
# BagiEngine Launcher (macOS)

cd "$(dirname "$0")"

# Setup venv if needed
if [ ! -f ".venv/bin/python" ]; then
    echo "[INFO] Setting up Python virtual environment..."
    source CI/setup_venv.sh
    if [ $? -ne 0 ]; then
        echo "[ERROR] Failed to setup virtual environment"
        read -p "Press any key to continue..."
        exit 1
    fi
fi

# Activate and run launcher
source .venv/bin/activate
python -m CI.launcher.main
