#!/bin/bash
# Keep all companion files together in the extracted folder.
cd -- "$(dirname -- "$0")" || exit 1
if ! command -v python3 >/dev/null 2>&1; then
    echo "Python 3 is required. Install it from https://www.python.org/downloads/macos/"
    read -r -p "Press Enter to close..."
    exit 1
fi
python3 ./open-amf-viewer-mac.py
result=$?
if [ "$result" -ne 0 ]; then read -r -p "Press Enter to close..."; fi
exit "$result"
