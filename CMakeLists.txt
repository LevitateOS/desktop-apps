# All four desktop apps in one build, for the image: configure, build and
# install this folder and a system tree gets the four programs, their menu
# entries and their polkit actions. Each app's own folder still builds alone.
cmake_minimum_required(VERSION 3.21)
project(levitate-desktop-apps VERSION 0.1 LANGUAGES CXX)

# Stories (--shots and what goes with it): ON for developers, OFF for the image.
# Each app's folder declares the same option, so it also works configured alone.
option(LEVITATE_STORIES "Compile the screenshot/story mode (--shots)" ON)

add_subdirectory(recipe)
add_subdirectory(recab)
add_subdirectory(disk)
add_subdirectory(updater)
