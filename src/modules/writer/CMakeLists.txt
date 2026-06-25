add_library(WriterModule STATIC
    WriterModule.cpp
    WriterModule.h
    WriterRibbon.cpp
    WriterRibbon.h
    WriterStatusBar.cpp
    WriterStatusBar.h
)

target_include_directories(WriterModule PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(WriterModule PUBLIC
    NativeOfficeCore
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::PrintSupport
)
