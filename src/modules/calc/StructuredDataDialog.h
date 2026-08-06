#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// StructuredDataDialog.h — paste JSON or YAML, see the table it becomes, import.
//
// The preview is the point. Flattening is lossy in ways that are obvious once
// you see the grid and invisible if you do not (nested arrays become indexed
// columns, missing fields become blanks), and the sheet is 26 columns wide, so
// a paste can silently lose data. Showing the result first, with the truncation
// spelled out, means the user commits to something they have already seen.
//
// Knows nothing about SpreadsheetModel: the row/column limits are passed in, so
// this dialog can serve Writer or anything else later without change.
// ─────────────────────────────────────────────────────────────────────────────

#include <QDialog>

#include "StructuredData.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;

namespace NativeOffice {

class StructuredDataDialog : public QDialog {
    Q_OBJECT

public:
    StructuredDataDialog(int maxRows, int maxCols, QWidget* parent = nullptr);

    // Valid only after exec() returns Accepted. Already clamped to the limits
    // handed to the constructor.
    [[nodiscard]] StructuredData::Table table() const { return m_clamped; }

    // True when the user asked to land the table at the current selection
    // rather than at A1.
    [[nodiscard]] bool insertAtSelection() const;

private:
    void reparse();                     // re-run on every edit; drives the preview
    void updatePreview();
    void setStatus(const QString& msg, bool isError);

    int m_maxRows;
    int m_maxCols;

    QPlainTextEdit* m_input   { nullptr };
    QComboBox*      m_format  { nullptr };
    QCheckBox*      m_atSel   { nullptr };
    QTableWidget*   m_preview { nullptr };
    QLabel*         m_status  { nullptr };
    QPushButton*    m_okBtn   { nullptr };

    StructuredData::Table m_parsed;     // what the text produced
    StructuredData::Table m_clamped;    // what will actually be written
};

} // namespace NativeOffice
