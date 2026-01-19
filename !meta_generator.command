#!/bin/bash
# BagiEngine Meta-Generator GUI Launcher

cd "$(dirname "$0")"

# Setup meta-generator environment first
echo "[INFO] Setting up Meta-Generator environment..."
source CI/meta_generator/setup_env.sh
if [ $? -ne 0 ]; then
    echo "[WARNING] Meta-Generator setup had issues, continuing anyway..."
fi

# Run Meta-Generator GUI
echo
echo "[INFO] Starting Meta-Generator GUI..."
if [ -f ".venv/bin/python" ]; then
    .venv/bin/python CI/meta_generator/meta_generator_gui.py
elif [ -f ".venv/bin/python3" ]; then
    .venv/bin/python3 CI/meta_generator/meta_generator_gui.py
else
    echo "[ERROR] Virtual environment not found. Please run !start.command first."
    read -p "Press any key to continue..."
    exit 1
fi
