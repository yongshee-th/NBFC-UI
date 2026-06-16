#!/bin/bash

# --- NBFC Fan Control UI Uninstaller ---
# This script removes the application and its associated files from the system.

echo "--- [1/2] Removing system files ---"

# Remove the binary
if [ -f /usr/local/bin/NBFC_UI ]; then
    echo "Removing binary: /usr/local/bin/NBFC_UI"
    sudo rm /usr/local/bin/NBFC_UI
fi

# Remove the desktop entry
if [ -f /usr/share/applications/nbfc-ui.desktop ]; then
    echo "Removing desktop entry: /usr/share/applications/nbfc-ui.desktop"
    sudo rm /usr/share/applications/nbfc-ui.desktop
fi

# Remove the icon
if [ -f /usr/share/icons/hicolor/256x256/apps/NBFC-fan-control-ui.png ]; then
    echo "Removing icon: /usr/share/icons/hicolor/256x256/apps/NBFC-fan-control-ui.png"
    sudo rm /usr/share/icons/hicolor/256x256/apps/NBFC-fan-control-ui.png
fi

# 2. Handle Config.json (Optional)
echo "--- [2/2] Handling configuration ---"
if [ -f /usr/local/bin/Config.json ]; then
    read -p "Do you want to remove the configuration file (Config.json)? (y/N) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        echo "Removing configuration: /usr/local/bin/Config.json"
        sudo rm /usr/local/bin/Config.json
    else
        echo "Preserving configuration file for future use."
    fi
fi

# Refresh desktop database
sudo update-desktop-database /usr/share/applications

echo "------------------------------------------------"
echo "Uninstallation Complete!"
echo "NBFC Fan Control UI has been removed from your system."
echo "------------------------------------------------"
