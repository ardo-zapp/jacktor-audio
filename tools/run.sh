#!/bin/bash

# Automatically exit if any command fails
set -e

# First argument defaults to 'amplifier' if none provided
TARGET="${1:-amplifier}"

echo "=========================================="
echo " Starting Workflow Automation"
echo "=========================================="

# 1. Run git pull
echo "--> [1/3] Running git pull..."
git pull

# 2. Run upload script based on target
case "$TARGET" in
    amplifier)
        echo "--> [2/3] Running upload.py (amplifier)..."
        python3 upload.py amplifier
        ;;
    panel)
        echo "--> [2/3] Running upload.py (panel)..."
        python3 upload.py panel
        ;;
    *)
        echo "❌ Error: Unknown target '$TARGET'! Use 'amplifier' or 'panel'."
        exit 1
        ;;
esac

# 3. If previous steps succeeded, launch cmd.py
echo "--> [3/3] Launching cmd.py..."
python3 cmd.py

echo "=========================================="
echo " Success: All tasks completed successfully!"
echo "=========================================="
