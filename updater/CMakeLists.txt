cmake_minimum_required(VERSION 3.21)
project(updater-gui VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Svg)

qt_add_executable(updater-gui
    src/main.cpp
    src/client.cpp
    src/pages.cpp
    src/window.cpp
    ../kit/parts.cpp
    ../kit/look.cpp
    ../kit/run.cpp
    ../kit/words.cpp
    ../kit/page.cpp
    ../kit/before.cpp
    ../kit/refresh.cpp
    ../kit/release.cpp
    ../kit/install.cpp
    ../kit/windowsize.cpp
)
target_include_directories(updater-gui PRIVATE ../kit)
target_compile_options(updater-gui PRIVATE -Wall -Wextra)
target_link_libraries(updater-gui PRIVATE Qt6::Widgets Qt6::Svg)

# For mutable installs only: the image builder ships these files on a system
# with one root and leaves them out of an A/B system.
include(GNUInstallDirs)
install(TARGETS updater-gui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/org.levitateos.updater.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES ../icons/org.levitateos.updater.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES data/org.levitateos.updater.policy DESTINATION ${CMAKE_INSTALL_DATADIR}/polkit-1/actions)
