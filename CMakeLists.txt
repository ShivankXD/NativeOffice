cmake_minimum_required(VERSION 3.21)

project(NativeOffice
    VERSION 0.1.0
    DESCRIPTION "NativeOffice - High-Performance Cross-Platform Desktop Office Suite"
    LANGUAGES CXX
)

# ─────────────────────────────────────────────────────────────────────────────
# C++ Standard
# ─────────────────────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ─────────────────────────────────────────────────────────────────────────────
# CMake policies & build type
# ─────────────────────────────────────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Qt 6 setup
# ─────────────────────────────────────────────────────────────────────────────
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Svg
    PrintSupport
    Charts
)

qt_standard_project_setup()

# ─────────────────────────────────────────────────────────────────────────────
# Global include directories
# ─────────────────────────────────────────────────────────────────────────────
include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
)

# ─────────────────────────────────────────────────────────────────────────────
# Subdirectories
# ─────────────────────────────────────────────────────────────────────────────
add_subdirectory(src/core)
add_subdirectory(src/modules/writer)
add_subdirectory(src/modules/calc)
add_subdirectory(src/modules/impress)
add_subdirectory(src/app)

# ─────────────────────────────────────────────────────────────────────────────
# Installation rules (future-proofing)
# ─────────────────────────────────────────────────────────────────────────────
install(TARGETS NativeOffice
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
