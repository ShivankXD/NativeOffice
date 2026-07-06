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
    Network
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
# PDFium (vendored prebuilt, third_party/pdfium) — the page-rendering engine
# for the PDF module. BSD-licensed; license texts ship from
# third_party/pdfium/licenses. The non-V8 build needs no icudtl.dat or other
# data files — just the DLL.
# ─────────────────────────────────────────────────────────────────────────────
add_library(pdfium SHARED IMPORTED GLOBAL)
set_target_properties(pdfium PROPERTIES
    IMPORTED_LOCATION             "${CMAKE_SOURCE_DIR}/third_party/pdfium/bin/pdfium.dll"
    IMPORTED_IMPLIB               "${CMAKE_SOURCE_DIR}/third_party/pdfium/lib/pdfium.dll.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/third_party/pdfium/include"
)

# ─────────────────────────────────────────────────────────────────────────────
# Subdirectories
# ─────────────────────────────────────────────────────────────────────────────
add_subdirectory(src/core)
add_subdirectory(src/modules/writer)
add_subdirectory(src/modules/calc)
add_subdirectory(src/modules/impress)
add_subdirectory(src/modules/pdf)
add_subdirectory(src/app)

# Dev-only smoke test for the PDFium integration (small console exe; not
# installed, not shipped).
qt_add_executable(PdfRenderSmoke tools/PdfRenderSmoke.cpp)
target_include_directories(PdfRenderSmoke PRIVATE ${CMAKE_SOURCE_DIR}/src/modules/pdf)
target_link_libraries(PdfRenderSmoke PRIVATE PdfModule Qt6::Gui)
add_custom_command(TARGET PdfRenderSmoke POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:pdfium>" "$<TARGET_FILE_DIR:PdfRenderSmoke>")

# ─────────────────────────────────────────────────────────────────────────────
# Installation rules (future-proofing)
# ─────────────────────────────────────────────────────────────────────────────
install(TARGETS NativeOffice
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
