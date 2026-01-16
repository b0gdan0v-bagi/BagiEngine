#!/bin/bash
# Script to open BagiEngine workspace in Cursor (macOS/Linux)
# This .command file can be double-clicked on macOS

echo "Opening BagiEngine workspace in Cursor..."

# Get the directory where the script is located
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
WORKSPACE_PATH="$SCRIPT_DIR/BagiEngine.code-workspace"

# Try to find and use Cursor
if command -v cursor &> /dev/null; then
    echo "Found Cursor in PATH"
    cursor "$WORKSPACE_PATH"
elif [ -d "/Applications/Cursor.app" ]; then
    echo "Found Cursor in Applications folder"
    open -a "Cursor" "$WORKSPACE_PATH"
elif [ -f "/usr/local/bin/cursor" ]; then
    echo "Found Cursor in /usr/local/bin"
    /usr/local/bin/cursor "$WORKSPACE_PATH"
elif [ -f "$HOME/.cursor/cursor" ]; then
    echo "Found Cursor in home directory"
    "$HOME/.cursor/cursor" "$WORKSPACE_PATH"
else
    echo "Cursor not found in standard paths, trying default application..."
    open "$WORKSPACE_PATH"
fi

echo ""
echo "Workspace opened successfully!"

# Keep terminal open for a moment to see the message
sleep 2
