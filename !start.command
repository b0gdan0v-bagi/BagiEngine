#!/bin/bash
# BagiEngine Launcher (macOS/Linux)

cd "$(dirname "$0")"

# Setup meta-generator environment first
echo "[INFO] Setting up Meta-Generator environment..."
source CI/meta_generator/setup_env.sh
if [ $? -ne 0 ]; then
    echo "[WARNING] Meta-Generator setup had issues, continuing..."
fi

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
