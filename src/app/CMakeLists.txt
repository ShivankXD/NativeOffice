qt_add_executable(NativeOffice
    main.cpp
    startscreen/StartScreen.cpp
    startscreen/StartScreen.h
    startscreen/SidebarWidget.cpp
    startscreen/SidebarWidget.h
    startscreen/TemplateBarWidget.cpp
    startscreen/TemplateBarWidget.h
    startscreen/RecentFilesWidget.cpp
    startscreen/RecentFilesWidget.h
    ${CMAKE_SOURCE_DIR}/resources/resources.qrc
)

target_include_directories(NativeOffice PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/modules/writer
    ${CMAKE_SOURCE_DIR}/src/modules/calc
    ${CMAKE_SOURCE_DIR}/src/modules/impress
)

target_link_libraries(NativeOffice PRIVATE
    NativeOfficeCore
    WriterModule
    CalcModule
    ImpressModule
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Svg
    Qt6::PrintSupport
)

set_target_properties(NativeOffice PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE    TRUE
)
