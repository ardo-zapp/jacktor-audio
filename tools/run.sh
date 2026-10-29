#!/bin/bash

# Automatically exit if any command fails
set -e

# First argument defaults to 'usb' if none provided
TARGET="${1:-usb}"

echo "=========================================="
echo " Starting Workflow Automation"
echo "=========================================="

# 1. Run git pull
echo "--> [1/3] Running git pull..."
git pull

# 2. Run upload script based on target
case "$TARGET" in
    usb|direct|local|amp|amplifier)
        echo "--> [2/3] Running upload.py (Direct USB)..."
        python3 upload.py usb
        ;;
    adb|android|panel)
        echo "--> [2/3] Running upload.py (via ADB)..."
        python3 upload.py adb
        ;;
    *)
        echo "❌ Error: Unknown target '$TARGET'! Use 'usb' or 'adb'."
        exit 1
        ;;
esac

# 3. If previous steps succeeded, launch cmd.py
echo "--> [3/3] Launching cmd.py..."
python3 cmd.py

echo "=========================================="
echo " Success: All tasks completed successfully!"
echo "=========================================="
