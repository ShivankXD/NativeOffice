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
    # Charts and pictures drawn over the cells. Split out of XlsxIo because the
    # drawing/chart schemas are their own world and the parser needs no ZIP.
    XlsxDrawing.cpp
    XlsxDrawing.h
    # Drawn shapes (banners, buttons, callouts, rules). Rendering them is its
    # own job: the parser hands over geometry, fill and text, this paints it.
    ShapeObject.cpp
    ShapeObject.h
    # JSON/YAML converter. Self-contained: delete these four files and the
    # importStructuredData/copySelectionAsJson/copySelectionAsYaml calls in
    # CalcModule to remove the feature entirely.
    StructuredData.cpp
    StructuredData.h
    StructuredDataDialog.cpp
    StructuredDataDialog.h
    # Data Cleanser. Also self-contained: delete these four files plus
    # showDataCleanser/runCleanserOp/requirePremiumFor in CalcModule.
    DataCleanser.cpp
    DataCleanser.h
    DataCleanserPanel.cpp
    DataCleanserPanel.h
    # SQL on Sheets. The only feature here that costs a new Qt module
    # (Qt6::Sql below): delete these four files, the showSqlPanel/runSqlQuery/
    # sendSqlResultToSheet calls in CalcModule, and that link entry.
    SheetSql.cpp
    SheetSql.h
    SheetSqlPanel.cpp
    SheetSqlPanel.h
    # Version history UI. The store itself lives in core/history/DocHistory
    # so Writer can share it.
    CalcHistoryPanel.cpp
    CalcHistoryPanel.h
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
    # SQL on Sheets only. Costs 2.19 MB in the shipped package: Qt6Sql.dll
    # (304 KB) plus the bundled SQLite driver plugin qsqlite.dll (1,936 KB).
    # See the header comment in SheetSql.h for why that was preferred to a
    # hand-written SQL subset. Note the packaging step must ship ONLY the
    # sqlite driver: the other sqldrivers plugins add ~5 MB of database clients
    # nothing here can use.
    Qt6::Sql
)
