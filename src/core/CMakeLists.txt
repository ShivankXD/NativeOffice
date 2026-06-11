add_library(NativeOfficeCore STATIC
    theme/ThemeManager.cpp
    theme/ThemeManager.h
    application/AppController.cpp
    application/AppController.h
    application/RecentFilesManager.cpp
    application/RecentFilesManager.h
    application/FileRouter.cpp
    application/FileRouter.h
)

target_include_directories(NativeOfficeCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(NativeOfficeCore PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)
