add_library(ImpressModule STATIC
    ImpressModule.cpp
    ImpressModule.h
    SlideData.h
    SlideScene.cpp
    SlideScene.h
    SlideThumbnailWidget.cpp
    SlideThumbnailWidget.h
    SlidePanelWidget.cpp
    SlidePanelWidget.h
    OutlineWidget.cpp
    OutlineWidget.h
    ImpressRibbon.cpp
    ImpressRibbon.h
    ImpressStatusBar.cpp
    ImpressStatusBar.h
    PresentModeWindow.cpp
    PresentModeWindow.h
)

target_include_directories(ImpressModule PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(ImpressModule PUBLIC
    NativeOfficeCore
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::PrintSupport
)
