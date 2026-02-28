#!/bin/bash
# LRS Flasher Launcher v3.5

# Get the absolute directory of the script
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$DIR"

# Check if venv exists, create if not
if [ ! -d "venv" ]; then
    echo "Creating virtual environment in $DIR/venv..."
    python3 -m venv venv
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
