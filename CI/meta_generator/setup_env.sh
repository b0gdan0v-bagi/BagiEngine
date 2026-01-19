#!/bin/bash

echo "============================================"
echo "BagiEngine Meta-Generator Environment Setup"
echo "============================================"
echo

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$(dirname "$SCRIPT_DIR")")"
VENV_DIR="$REPO_ROOT/.venv"

# Check Python
if ! command -v python3 &> /dev/null; then
    echo "[ERROR] Python3 not found. Please install Python 3.10+"
    echo "  Ubuntu/Debian: sudo apt install python3 python3-pip python3-venv"
    echo "  macOS: brew install python3"
    exit 1
fi
echo "[OK] Python3 found: $(python3 --version)"

# Check/Create venv if not exists
if [ ! -f "$VENV_DIR/bin/python3" ]; then
    echo
    echo "[INFO] Virtual environment not found. Creating..."
    python3 -m venv "$VENV_DIR"
    if [ $? -ne 0 ]; then
        echo "[ERROR] Failed to create virtual environment"
        exit 1
    fi
    echo "[OK] Virtual environment created"
fi

# Activate venv
source "$VENV_DIR/bin/activate"

# Install pip packages
echo
echo "Installing Python dependencies..."
pip install clang pyqt6 jinja2 --quiet
if [ $? -ne 0 ]; then
    echo "[ERROR] Failed to install Python packages"
    exit 1
fi
echo "[OK] Python packages installed"

# Check LLVM
echo
echo "Checking LLVM installation..."

# Check environment variable
if [ -n "$LIBCLANG_PATH" ]; then
    if [ -f "$LIBCLANG_PATH/libclang.so" ] || [ -f "$LIBCLANG_PATH/libclang.dylib" ]; then
        echo "[OK] LLVM found via LIBCLANG_PATH: $LIBCLANG_PATH"
        LLVM_FOUND=1
    fi
fi

# Check llvm-config
if [ -z "$LLVM_FOUND" ] && command -v llvm-config &> /dev/null; then
    LLVM_LIB=$(llvm-config --libdir 2>/dev/null)
    if [ -n "$LLVM_LIB" ] && [ -d "$LLVM_LIB" ]; then
        echo "[OK] LLVM found via llvm-config: $LLVM_LIB"
        echo
        echo "Recommendation: Add to your shell profile:"
        echo "  export LIBCLANG_PATH=$LLVM_LIB"
        LLVM_FOUND=1
    fi
fi

# Check common Linux paths
if [ -z "$LLVM_FOUND" ]; then
    for version in 18 17 16 15 14; do
        if [ -f "/usr/lib/llvm-$version/lib/libclang.so" ]; then
            echo "[OK] LLVM found at: /usr/lib/llvm-$version/lib"
            echo
            echo "Recommendation: export LIBCLANG_PATH=/usr/lib/llvm-$version/lib"
            LLVM_FOUND=1
            break
        fi
    done
fi

# Check macOS Homebrew paths
if [ -z "$LLVM_FOUND" ] && [ "$(uname)" = "Darwin" ]; then
    if [ -f "/opt/homebrew/opt/llvm/lib/libclang.dylib" ]; then
        echo "[OK] LLVM found at: /opt/homebrew/opt/llvm/lib"
        echo
        echo "Recommendation: export LIBCLANG_PATH=/opt/homebrew/opt/llvm/lib"
        LLVM_FOUND=1
    elif [ -f "/usr/local/opt/llvm/lib/libclang.dylib" ]; then
        echo "[OK] LLVM found at: /usr/local/opt/llvm/lib"
        echo
        echo "Recommendation: export LIBCLANG_PATH=/usr/local/opt/llvm/lib"
        LLVM_FOUND=1
    fi
fi

if [ -z "$LLVM_FOUND" ]; then
    echo "[WARNING] LLVM/libclang not found automatically."
    echo
    echo "Please install LLVM:"
    echo "  Ubuntu/Debian: sudo apt install libclang-dev llvm"
    echo "  Fedora: sudo dnf install clang-devel llvm"
    echo "  macOS: brew install llvm"
    echo
    echo "Then set LIBCLANG_PATH:"
    echo "  export LIBCLANG_PATH=/usr/lib/llvm-16/lib  # adjust version"
    echo
    echo "Note: Meta-Generator will fall back to regex parsing without LLVM."
fi

echo
echo "============================================"
echo "Setup complete! You can now use meta_generator."
echo
echo "  CLI: python3 $SCRIPT_DIR/meta_generator.py --help"
echo "  GUI: python3 $SCRIPT_DIR/meta_generator_gui.py"
echo "============================================"
