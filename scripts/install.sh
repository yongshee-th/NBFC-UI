#!/bin/bash

# --- NBFC Fan Control UI Installer ---
# Run from project root: ./scripts/install.sh

echo "--- [1/4] Installing dependencies ---"
sudo apt update
sudo apt install -y build-essential cmake qtbase5-dev qtchooser qt5-qmake qtbase5-dev-tools libqt5uitools5-dev

echo "--- [2/4] Building project ---"
cd "$(dirname "$0")/.."
mkdir -p build
cd build
cmake ..
make -j$(nproc)

echo "--- [3/4] Installing to system ---"
sudo make install

# Ensure icons and desktop entry are correctly handled
sudo mkdir -p /usr/share/icons/hicolor/256x256/apps
sudo cp ../resources/icon.png /usr/share/icons/hicolor/256x256/apps/NBFC-fan-control-ui.png

if [ ! -f /usr/local/bin/Config.json ]; then
    sudo cp ../Config.json /usr/local/bin/
fi

echo "--- [4/4] Finishing up ---"
sudo update-desktop-database /usr/share/applications

echo "------------------------------------------------"
echo "Installation Complete!"
echo "Run 'NBFC_UI' from terminal or find it in your menu."
echo "------------------------------------------------"
