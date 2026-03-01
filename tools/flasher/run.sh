#!/bin/bash
# Thanda LoRa Flasher Launcher v0.4.1-alpha

# Get the absolute directory of the script
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$DIR"

# Check if venv is valid, create/re-create if not
if [ ! -f "venv/bin/activate" ]; then
    echo "Virtual environment missing or broken. Initializing..."
    rm -rf venv
    python3 -m venv venv || { echo "ERROR: Failed to create venv. Is 'python3-venv' installed?"; exit 1; }
fi

# Activate venv
source venv/bin/activate

# Install/Update dependencies
echo "Verifying dependencies from $DIR/requirements.txt..."
if [ -f "requirements.txt" ]; then
    pip install -q -r requirements.txt
else
    echo "ERROR: requirements.txt not found in $DIR"
    exit 1
fi

# Launch app
export PYTHONPATH="$DIR/.."
echo "Launching LRS Flasher v3.8..."
python3 main.py
