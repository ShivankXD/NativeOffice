cmake_minimum_required(VERSION 3.21)

project(NativeOffice
    VERSION 0.1.0
    DESCRIPTION "NativeOffice - High-Performance Cross-Platform Desktop Office Suite"
    LANGUAGES CXX C
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
# md4c (vendored source, third_party/md4c) — the CommonMark parser behind the
# Markdown Editor tool (Home → Tools). MIT-licensed; a single small C library
# with no dependencies beyond the C standard library, compiled straight into
# the app (no DLL, unlike pdfium). md4c.c parses; md4c-html.c renders the
# parse tree to an HTML fragment we style + colorize ourselves.
# ─────────────────────────────────────────────────────────────────────────────
add_library(md4c STATIC
    third_party/md4c/md4c.c
    third_party/md4c/md4c.h
    third_party/md4c/md4c-html.c
    third_party/md4c/md4c-html.h
    third_party/md4c/entity.c
    third_party/md4c/entity.h
)
target_include_directories(md4c PUBLIC ${CMAKE_SOURCE_DIR}/third_party/md4c)

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
get_target_property(_qtcore_inc2 Qt6::Core INTERFACE_INCLUDE_DIRECTORIES)
foreach(_d IN LISTS _qtcore_inc2)
    if(EXISTS "${_d}/${Qt6Core_VERSION}/QtCore/private/qzipreader_p.h")
        target_include_directories(PdfRenderSmoke PRIVATE "${_d}/${Qt6Core_VERSION}")
    endif()
endforeach()
target_link_libraries(PdfRenderSmoke PRIVATE PdfModule Qt6::Gui)
add_custom_command(TARGET PdfRenderSmoke POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:pdfium>" "$<TARGET_FILE_DIR:PdfRenderSmoke>")

# Dev-only check that the export watermark's link annotation is present on every
# page, scoped to the artwork, and not triggered by clicks near it.
# The brand mark lives in resources.qrc, which the app target owns; the smoke
# test needs it too or it would render the artwork without its icon and quietly
# "pass" on something the user would never see.
qt_add_executable(WatermarkSmoke tools/WatermarkSmoke.cpp
    ${CMAKE_SOURCE_DIR}/resources/resources.qrc)
target_link_libraries(WatermarkSmoke PRIVATE NativeOfficeCore WriterModule pdfium Qt6::Gui)
target_include_directories(WatermarkSmoke PRIVATE ${CMAKE_SOURCE_DIR}/src/modules/writer)
foreach(_d IN LISTS _qtcore_inc2)
    if(EXISTS "${_d}/${Qt6Core_VERSION}/QtCore/private/qzipreader_p.h")
        target_include_directories(WatermarkSmoke PRIVATE "${_d}/${Qt6Core_VERSION}")
    endif()
endforeach()
add_custom_command(TARGET WatermarkSmoke POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:pdfium>" "$<TARGET_FILE_DIR:WatermarkSmoke>")

# ─────────────────────────────────────────────────────────────────────────────
# Installation rules (future-proofing)
# ─────────────────────────────────────────────────────────────────────────────
install(TARGETS NativeOffice
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
