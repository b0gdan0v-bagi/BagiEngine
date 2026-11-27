#!/bin/bash
# The double-click entry for CI/setup_venv.sh (macOS, Linux); arguments go on to it.
cd "$(dirname "$0")" || exit 1
CI/setup_venv.sh "$@"
STATUS=$?
if [ -t 0 ]; then
    read -r -p "Press Enter to close..."
fi
exit "$STATUS"
