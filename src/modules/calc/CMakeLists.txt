add_library(CalcModule STATIC
    CalcModule.cpp
    CalcModule.h
    SpreadsheetModel.cpp
    SpreadsheetModel.h
    FormulaEngine.cpp
    FormulaEngine.h
    CalcHeaderView.cpp
    CalcHeaderView.h
    CalcIcons.cpp
    CalcIcons.h
    ChartObject.cpp
    ChartObject.h
    ChartSpec.h
    CondFormat.h
    CalcCommands.cpp
    CalcCommands.h
    Cell.h
    XlsxIo.cpp
    XlsxIo.h
    # JSON/YAML converter. Self-contained: delete these four files and the
    # importStructuredData/copySelectionAsJson/copySelectionAsYaml calls in
    # CalcModule to remove the feature entirely.
    StructuredData.cpp
    StructuredData.h
    StructuredDataDialog.cpp
    StructuredDataDialog.h
)

target_include_directories(CalcModule PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(CalcModule PUBLIC
    NativeOfficeCore
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Svg
    Qt6::Charts
    Qt6::PrintSupport
)
