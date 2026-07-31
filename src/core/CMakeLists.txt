add_library(NativeOfficeCore STATIC
    theme/ThemeManager.cpp
    theme/ThemeManager.h
    application/AppController.cpp
    application/AppController.h
    application/RecentFilesManager.cpp
    application/RecentFilesManager.h
    application/FileRouter.cpp
    application/FileRouter.h
    application/UpdateChecker.cpp
    application/UpdateChecker.h
    auth/AuthManager.cpp
    auth/AuthManager.h
    auth/SecureStore.cpp
    auth/SecureStore.h
    common/BrandBar.cpp
    common/BrandBar.h
    watermark/Watermark.cpp
    watermark/Watermark.h
    watermark/WatermarkPdf.cpp
    watermark/WatermarkPdf.h
    watermark/WatermarkOoxml.cpp
    watermark/WatermarkOoxml.h
)

target_include_directories(NativeOfficeCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(NativeOfficeCore PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
)

# The watermark's PDF path stamps and links pages through PDFium. Qt's
# QPdfWriter can draw the artwork but exposes no annotation API at all, so the
# link has to be attached by reopening the finished file with PDFium.
target_link_libraries(NativeOfficeCore PRIVATE pdfium)

# SecureStore uses DPAPI (CryptProtectData) on Windows.
if(WIN32)
    target_link_libraries(NativeOfficeCore PUBLIC crypt32)
endif()
