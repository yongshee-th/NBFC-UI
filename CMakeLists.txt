cmake_minimum_required(VERSION 3.10)

project(NBFC-UI VERSION 2.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt5 COMPONENTS Core Widgets Gui UiTools REQUIRED)

# Specify sources and resources with new paths
add_executable(NBFC_UI
    src/main.cpp
    resources/resources.qrc
)

target_link_libraries(NBFC_UI
    Qt5::Core
    Qt5::Widgets
    Qt5::Gui
    Qt5::UiTools
)

install(TARGETS NBFC_UI DESTINATION bin)
install(FILES resources/icon.png DESTINATION share/icons/hicolor/256x256/apps)
install(FILES nbfc-ui.desktop DESTINATION share/applications)
