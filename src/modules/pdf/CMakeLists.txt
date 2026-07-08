add_library(PdfModule STATIC
    PdfObject.h
    PdfDocument.cpp
    PdfDocument.h
    PdfWriter.cpp
    PdfWriter.h
    PdfOps.cpp
    PdfOps.h
    PdfRenderer.cpp
    PdfRenderer.h
    PdfEditSession.cpp
    PdfEditSession.h
    PdfViewer.cpp
    PdfViewer.h
    PdfPanels.cpp
    PdfPanels.h
    PdfRibbon.cpp
    PdfRibbon.h
    PdfOrganizer.cpp
    PdfOrganizer.h
    PdfRebuild.h
    PdfDecor.cpp
    PdfDecor.h
    PdfDecorUi.cpp
    PdfDecorUi.h
    PdfAnnots.cpp
    PdfAnnots.h
    PdfForms.cpp
    PdfForms.h
    PdfSignature.cpp
    PdfSignature.h
    PdfCrypto.cpp
    PdfCrypto.h
    PdfCryptoUi.cpp
    PdfCryptoUi.h
    PdfSign.cpp
    PdfSign.h
    PdfSignUi.cpp
    PdfSignUi.h
    PdfConvert.cpp
    PdfConvert.h
    PdfOcr.cpp
    PdfOcr.h
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
    Qt6::PrintSupport
    pdfium
)

# PdfConvert uses Qt's QZipWriter (OOXML export). It's Q_CORE_EXPORT in
# Qt6Core.dll but declared in a private header; add the private include path
# the same way WriterModule does (no Qt6::CorePrivate target dependency).
get_target_property(_qtcore_inc Qt6::Core INTERFACE_INCLUDE_DIRECTORIES)
foreach(_d IN LISTS _qtcore_inc)
    if(EXISTS "${_d}/${Qt6Core_VERSION}/QtCore/private/qzipwriter_p.h")
        target_include_directories(PdfModule PRIVATE "${_d}/${Qt6Core_VERSION}")
    endif()
endforeach()

# CNG (bcrypt) for the standard security handler (AES-256 encryption) and
# crypt32 for certificate signatures / timestamps / validation.
if(WIN32)
    # bcrypt: AES-256 encryption; crypt32/ncrypt: certificate signatures;
    # windowsapp: C++/WinRT umbrella for Windows.Media.Ocr.
    target_link_libraries(PdfModule PUBLIC bcrypt crypt32 ncrypt windowsapp)
endif()
