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
    settings/UsageStats.cpp
    settings/UsageStats.h
    watermark/Watermark.cpp
    watermark/Watermark.h
    watermark/WatermarkPdf.cpp
    watermark/WatermarkPdf.h
    watermark/WatermarkOoxml.cpp
    watermark/WatermarkOoxml.h
    # Local version history. Generic key/value snapshot store, so Calc and
    # Writer can both use it; only Calc is wired up so far.
    history/DocHistory.cpp
    history/DocHistory.h
    # Stasis assistant. Lives in core rather than in a module because the panel
    # is owned by the shell and outlives any one document tab.
    ai/AiTypes.h
    ai/AiDocumentAgent.cpp
    ai/AiDocumentAgent.h
    ai/AiChatStore.cpp
    ai/AiChatStore.h
    ai/AiConsent.cpp
    ai/AiConsent.h
    ai/AiQuota.cpp
    ai/AiQuota.h
    ai/StasisClient.cpp
    ai/StasisClient.h
    ai/ui/AiSidebar.cpp
    ai/ui/AiSidebar.h
    ai/ui/AiConsentDialog.cpp
    ai/ui/AiConsentDialog.h
    ai/ui/AiToast.cpp
    ai/ui/AiToast.h
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
