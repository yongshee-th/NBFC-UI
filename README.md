# NBFC Fan Control (Linux)
A fan control program for notebooks, developed with C++ and Qt5. It connects to the NBFC (Notebook Fan Control) system to monitor temperature and fan speed in real-time.
![Program example image](2026-03-29_21-51.png)

## Features
* Real-time Monitoring: Displays CPU/GPU temperature and fan speed (%) read from the actual system.

* Manual Control: Adjust fan speed independently between CPU and GPU via sliders.

* Auto Mode: Switch back to automatic fan control with a single button.

* Theme System: Supports changing themes (Dark, Light, Blue, Custom) via the Config.json file.

* Linux Optimized: Designed to work on Linux environments (e.g., Kubuntu, Mint).

## Prerequisites
Before use, ensure your machine has the following installed:
1. NBFC-Linux: (Very important) The program requires the nbfc command to retrieve values.
    ```
    # Check if installed:
    nbfc status -a
    ```
2. Qt5 Development Libraries:
    ```
    sudo apt install qtbase5-dev qtchooser qt5-qmake qtbase5-dev-tools libqt5uitools5-dev
    ```
3. Build Tools: g++ and make

## How to Build & Run
1. Compilation (Manual Build)

    Use the g++ command, ensuring the libraries are in the correct order to prevent errors:
    ```
    g++ main.cpp -o NBFC_fan \
    -I/usr/include/x86_64-linux-gnu/qt5 \
    -I/usr/include/x86_64-linux-gnu/qt5/QtWidgets \
    -I/usr/include/x86_64-linux-gnu/qt5/QtUiTools \
    -lQt5UiTools -lQt5Widgets -lQt5Gui -lQt5Core -fPIC
    ```
2. Running the Program

    Make sure the UI.ui and Config.json files are in the same folder as the Executable file:
    ```
    ./NBFC_fan
    ```
## Configuration & Themes
You can customize the program's colors in the Config.json file. The program will automatically load the stylesheet without needing to recompile.
## Project Structure
* main.cpp: The main logic of the program and signal connections. (Signals/Slots)
* UI.ui: Program interface designed using Qt Designer
* Config.json: File storing color and style settings for each theme
* .vscode/: Build Task settings for VS Code

## 📝 License

This project is licensed under the [CC BY-NC 4.0](http://creativecommons.org/licenses/by-nc/4.0/) license.
(Attribution-NonCommercial 4.0 International)
You are free to use or modify it, **but you may not use it commercially or sell it**, and please give credit to the original developer.