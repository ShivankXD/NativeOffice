if(WIN32)
    set(NATIVEOFFICE_APP_ICON ${CMAKE_SOURCE_DIR}/resources/nativeoffice.rc)
endif()

qt_add_executable(NativeOffice
    main.cpp
    ${NATIVEOFFICE_APP_ICON}
    startscreen/StartScreen.cpp
    startscreen/StartScreen.h
    startscreen/LucideIcons.h
    startscreen/SidebarWidget.cpp
    startscreen/SidebarWidget.h
    startscreen/TemplateBarWidget.cpp
    startscreen/TemplateBarWidget.h
    startscreen/RecentFilesWidget.cpp
    startscreen/RecentFilesWidget.h
    startscreen/SplashScreen.cpp
    startscreen/SplashScreen.h
    auth/LoginGate.cpp
    auth/LoginGate.h
    auth/InstanceGuard.cpp
    auth/InstanceGuard.h
    settings/SettingsDialog.cpp
    settings/SettingsDialog.h
    common/Avatars.h
    ${CMAKE_SOURCE_DIR}/resources/resources.qrc
)

target_include_directories(NativeOffice PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/modules/writer
    ${CMAKE_SOURCE_DIR}/src/modules/calc
    ${CMAKE_SOURCE_DIR}/src/modules/impress
    ${CMAKE_SOURCE_DIR}/src/modules/pdf
)

target_link_libraries(NativeOffice PRIVATE
    NativeOfficeCore
    WriterModule
    CalcModule
    ImpressModule
    PdfModule
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Svg
    Qt6::PrintSupport
    Qt6::Network
)

set_target_properties(NativeOffice PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE    TRUE
)

# PdfModule links pdfium.dll — stage it beside the exe so the app starts
# without any extra PATH setup (same reason as the windeployqt step below).
add_custom_command(TARGET NativeOffice POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:pdfium>" "$<TARGET_FILE_DIR:NativeOffice>"
    COMMENT "Staging pdfium.dll next to NativeOffice.exe")

# Deploy the Qt runtime beside the exe after every build. Without this the
# exe only runs when Qt's bin is on PATH — and processes we don't launch
# (e.g. the browser spawning us via the nativeoffice:// protocol handler)
# inherit *their* parent's PATH and crash with "Qt6*.dll was not found".
if(WIN32)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt
                 HINTS "${Qt6_DIR}/../../../bin")
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET NativeOffice POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}" --no-translations
                    "$<TARGET_FILE:NativeOffice>"
            COMMENT "windeployqt: staging Qt runtime next to NativeOffice.exe")
    endif()
endif()
