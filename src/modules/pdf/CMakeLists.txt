add_library(PdfModule STATIC
    PdfObject.h
    PdfDocument.cpp
    PdfDocument.h
    PdfWriter.cpp
    PdfWriter.h
    PdfOps.cpp
    PdfOps.h
    PdfModule.cpp
    PdfModule.h
)

target_include_directories(PdfModule PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Flate (de)compression for xref/object streams and the Compress tool goes
# through Qt's own qCompress()/qUncompress() (see PdfDocument.cpp /
# PdfOps.cpp) rather than linking zlib directly, so no extra dependency is
# needed here beyond Qt itself.
target_link_libraries(PdfModule PUBLIC
    NativeOfficeCore
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Svg
)
