cmake_minimum_required(VERSION 3.21)
project(recab-gui VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# The stories (--shots and what goes with it) are for developers working from a
# checkout. The OS builder configures with -DLEVITATE_STORIES=OFF: the story
# sources are then not compiled and the options do not exist in the program.
option(LEVITATE_STORIES "Compile the screenshot/story mode (--shots)" ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Svg)

qt_add_executable(recab-gui
    src/main.cpp
    src/status.cpp
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
if(LEVITATE_STORIES)
    target_sources(recab-gui PRIVATE
        src/shots.cpp
    )
else()
    target_compile_definitions(recab-gui PRIVATE LEVITATE_NO_STORIES)
endif()
target_include_directories(recab-gui PRIVATE ../kit)
target_compile_options(recab-gui PRIVATE -Wall -Wextra)
target_link_libraries(recab-gui PRIVATE Qt6::Widgets Qt6::Svg)

include(GNUInstallDirs)
install(TARGETS recab-gui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/org.levitateos.recab.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES ../icons/org.levitateos.recab.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES data/org.levitateos.recab.policy DESTINATION ${CMAKE_INSTALL_DATADIR}/polkit-1/actions)
