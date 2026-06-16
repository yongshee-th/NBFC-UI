#!/bin/bash

# --- NBFC Fan Control UI Test Script ---
# Run from project root: ./scripts/test.sh

echo "--- Starting Local Build for Testing ---"

# Navigate to project root
cd "$(dirname "$0")/.."

# 1. Create and enter build directory
mkdir -p build
cd build

# 2. Run CMake and Make
echo "Compiling..."
cmake ..
make -j$(nproc)

if [ $? -eq 0 ]; then
    echo "Build Successful!"
    
    # 3. Copy Config.json to the build directory
    cp ../Config.json .
    
    # 4. Run the application
    echo "Launching application for testing..."
    ./NBFC_UI
else
    echo "Build Failed. Please check the errors above."
    exit 1
fi
