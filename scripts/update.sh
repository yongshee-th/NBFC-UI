#!/bin/bash

# --- NBFC Fan Control UI Updater ---
# This script pulls the latest changes from Git and runs the installer.

echo "--- [1/3] Checking for updates from Git ---"
cd "$(dirname "$0")/.."

# Check if it's a git repo
if [ -d .git ]; then
    git fetch origin
    LOCAL=$(git rev-parse HEAD)
    REMOTE=$(git rev-parse @{u})

    if [ $LOCAL = $REMOTE ]; then
        echo "You are already up to date."
        read -p "Do you want to force a re-install? (y/N) " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            exit 0
        fi
    else
        echo "New updates available. Pulling changes..."
        git pull
    fi
else
    echo "Not a git repository. Skipping git pull."
fi

echo "--- [2/3] Running installer ---"
./scripts/install.sh

echo "--- [3/3] Update Complete ---"
echo "Please restart NBFC Fan Control to apply changes."
