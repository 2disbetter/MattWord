cmake_minimum_required(VERSION 3.5)

project(MattWord LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 COMPONENTS Widgets PrintSupport REQUIRED)

add_executable(MattWord
        main.cpp
        mainwindow.h
        mainwindow.cpp
        spellchecker.h
        spellchecker.cpp
)

target_link_libraries(MattWord PRIVATE Qt6::Widgets Qt6::PrintSupport)