add_library(WriterModule STATIC
    WriterModule.cpp
    WriterModule.h
    WriterRibbon.cpp
    WriterRibbon.h
    WriterStatusBar.cpp
    WriterStatusBar.h
    PagedTextEdit.cpp
    PagedTextEdit.h
    WriterTableOps.h
    DocxIo.cpp
    DocxIo.h
    SpellChecker.cpp
    SpellChecker.h
    SpellHighlighter.cpp
    SpellHighlighter.h
    WriterStyles.cpp
    WriterStyles.h
    WriterListOps.h
    WriterRuler.cpp
    WriterRuler.h
    AutoCorrect.cpp
    AutoCorrect.h
)

target_include_directories(WriterModule PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# DocxIo uses Qt's QZipReader/QZipWriter. The classes are Q_CORE_EXPORT (in
# Qt6Core.dll) but live in private headers, so add the private include path
# manually rather than depending on a Qt6::CorePrivate CMake target.
get_target_property(_qtcore_inc Qt6::Core INTERFACE_INCLUDE_DIRECTORIES)
foreach(_d IN LISTS _qtcore_inc)
    if(EXISTS "${_d}/${Qt6Core_VERSION}/QtCore/private/qzipreader_p.h")
        target_include_directories(WriterModule PRIVATE "${_d}/${Qt6Core_VERSION}")
    endif()
endforeach()

target_link_libraries(WriterModule PUBLIC
    NativeOfficeCore
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::PrintSupport
)

# Spell-check dictionary. SpellChecker searches next to the executable first; the
# source path is compiled in as a dev fallback so it works from the build tree.
target_compile_definitions(WriterModule PRIVATE
    SPELL_DICT_SOURCE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/dict/en_US.txt"
)

# Deploy the dictionary beside the final executable so installed builds find it.
add_custom_command(TARGET WriterModule POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/dict/en_US.txt"
            "$<TARGET_FILE_DIR:NativeOffice>/dictionaries/en_US.txt"
    VERBATIM
)
