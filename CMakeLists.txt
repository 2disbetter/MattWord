cmake_minimum_required(VERSION 3.16)

project(MattWord LANGUAGES CXX C)   # C for the bundled miniz zip library

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 COMPONENTS Widgets PrintSupport REQUIRED)

add_executable(MattWord
    WIN32                 # On Windows, build a GUI app (no console window pops up)
    main.cpp
    mainwindow.h
    mainwindow.cpp
    spellchecker.h
    spellchecker.cpp
    drawingcanvas.h
    drawingcanvas.cpp
    docxconverter.h
    docxconverter.cpp
    miniz.h
    miniz.c               # bundled single-file zip library (public domain)
)

target_link_libraries(MattWord PRIVATE Qt6::Widgets Qt6::PrintSupport)

# MSVC compiles source as the system codepage by default, which mangles the
# UTF-8 string literals in the code (e.g. the "—" em dash in the window title).
# Force UTF-8 so those literals compile and display correctly.
if (MSVC)
    target_compile_options(MattWord PRIVATE /utf-8)
endif()
