// ─────────────────────────────────────────────────────────────────────────────
// PdfCryptoUi.cpp — see PdfCryptoUi.h.
// ─────────────────────────────────────────────────────────────────────────────
#include "PdfCryptoUi.h"
#include "core/theme/ThemeManager.h"

#include <QCheckBox>
#include <QDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace NativeOffice::Pdf {

EncryptDialogResult runEncryptDialog(QWidget* parent) {
    EncryptDialogResult result;

    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Encrypt with Password"));
    dlg.setStyleSheet(ThemeManager::inputDialogStyleSheet());
    dlg.setMinimumWidth(440);
    auto* v = new QVBoxLayout(&dlg);

    const QString hint = QObject::tr("6-128 characters, case-sensitive");

    // ── Password for opening ────────────────────────────────────────────
    auto* openCheck = new QCheckBox(QObject::tr("Password for opening"), &dlg);
    auto* openPwd = new QLineEdit(&dlg);
    auto* openConfirm = new QLineEdit(&dlg);
    openPwd->setEchoMode(QLineEdit::Password);
    openConfirm->setEchoMode(QLineEdit::Password);
    openPwd->setPlaceholderText(hint);
    openConfirm->setPlaceholderText(QObject::tr("Re-enter password"));
    auto* openForm = new QFormLayout;
    openForm->addRow(QObject::tr("Password"), openPwd);
    openForm->addRow(QObject::tr("Confirm password"), openConfirm);

    // ── Password for editing and extracting ─────────────────────────────
    auto* editCheck = new QCheckBox(QObject::tr("Password for editing and extracting"), &dlg);
    auto* editPwd = new QLineEdit(&dlg);
    auto* editConfirm = new QLineEdit(&dlg);
    editPwd->setEchoMode(QLineEdit::Password);
    editConfirm->setEchoMode(QLineEdit::Password);
    editPwd->setPlaceholderText(hint);
    editConfirm->setPlaceholderText(QObject::tr("Re-enter password"));
    auto* editForm = new QFormLayout;
    editForm->addRow(QObject::tr("Password"), editPwd);
    editForm->addRow(QObject::tr("Confirm password"), editConfirm);

    auto setEnabled = [](QFormLayout* form, bool on) {
        for (int r = 0; r < form->rowCount(); ++r)
            if (auto* w = form->itemAt(r, QFormLayout::FieldRole))
                if (w->widget()) w->widget()->setEnabled(on);
    };
    setEnabled(openForm, false);
    setEnabled(editForm, false);
    QObject::connect(openCheck, &QCheckBox::toggled, &dlg, [=] { setEnabled(openForm, openCheck->isChecked()); });
    QObject::connect(editCheck, &QCheckBox::toggled, &dlg, [=] { setEnabled(editForm, editCheck->isChecked()); });

    v->addWidget(openCheck);
    v->addLayout(openForm);
    v->addSpacing(6);
    v->addWidget(editCheck);
    v->addLayout(editForm);
    v->addSpacing(8);

    // ── Also restrict ───────────────────────────────────────────────────
    auto* restrictLabel = new QLabel(QObject::tr("Also restrict:"), &dlg);
    auto* selectAll = new QCheckBox(QObject::tr("Select all"), &dlg);
    auto* restrictRow = new QHBoxLayout;
    restrictRow->addWidget(restrictLabel);
    restrictRow->addWidget(selectAll);
    restrictRow->addStretch();
    v->addLayout(restrictRow);

    auto* grid = new QGridLayout;
    auto* cPrint    = new QCheckBox(QObject::tr("Printing"), &dlg);
    auto* cCopy     = new QCheckBox(QObject::tr("Copying"), &dlg);
    auto* cComment  = new QCheckBox(QObject::tr("Commenting"), &dlg);
    auto* cAssemble = new QCheckBox(QObject::tr("Inserting and deleting pages"), &dlg);
    auto* cForms    = new QCheckBox(QObject::tr("Forms filling and commenting"), &dlg);
    grid->addWidget(cPrint,    0, 0);
    grid->addWidget(cCopy,     0, 1);
    grid->addWidget(cComment,  1, 0);
    grid->addWidget(cAssemble, 1, 1);
    grid->addWidget(cForms,    2, 0);
    v->addLayout(grid);

    const QList<QCheckBox*> restrictions = { cPrint, cCopy, cComment, cAssemble, cForms };
    QObject::connect(selectAll, &QCheckBox::toggled, &dlg, [=](bool on) {
        for (auto* c : restrictions) c->setChecked(on);
    });
    for (auto* c : restrictions)
        QObject::connect(c, &QCheckBox::toggled, &dlg, [=] {
            bool all = true;
            for (auto* r : restrictions) all = all && r->isChecked();
            QSignalBlocker b(selectAll);
            selectAll->setChecked(all);
        });

    auto* warn = new QLabel(QObject::tr("*Keep your password secure. It cannot be recovered if forgotten."), &dlg);
    warn->setWordWrap(true);
    warn->setStyleSheet("color:#9AA4B8; font-size:8pt;");
    v->addSpacing(6);
    v->addWidget(warn);

    // ── Confirm / Cancel ────────────────────────────────────────────────
    auto* buttonsRow = new QHBoxLayout;
    buttonsRow->addStretch();
    auto* confirmBtn = new QPushButton(QObject::tr("Confirm"), &dlg);
    auto* cancelBtn  = new QPushButton(QObject::tr("Cancel"), &dlg);
    confirmBtn->setDefault(true);
    buttonsRow->addWidget(confirmBtn);
    buttonsRow->addWidget(cancelBtn);
    v->addLayout(buttonsRow);

    QObject::connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    QObject::connect(confirmBtn, &QPushButton::clicked, &dlg, [&] {
        if (!openCheck->isChecked() && !editCheck->isChecked()) {
            QMessageBox::warning(&dlg, QObject::tr("Encrypt"),
                QObject::tr("Enable at least one password section."));
            return;
        }
        auto validate = [&](QLineEdit* p, QLineEdit* c, const QString& label) -> bool {
            const QString pw = p->text();
            if (pw.size() < 6 || pw.size() > 128) {
                QMessageBox::warning(&dlg, QObject::tr("Encrypt"),
                    QObject::tr("%1 must be 6-128 characters.").arg(label));
                return false;
            }
            if (pw != c->text()) {
                QMessageBox::warning(&dlg, QObject::tr("Encrypt"),
                    QObject::tr("%1 fields do not match.").arg(label));
                return false;
            }
            return true;
        };
        if (openCheck->isChecked() && !validate(openPwd, openConfirm, QObject::tr("Open password"))) return;
        if (editCheck->isChecked() && !validate(editPwd, editConfirm, QObject::tr("Edit password"))) return;
        dlg.accept();
    });

    if (dlg.exec() != QDialog::Accepted) return result;

    result.confirmed = true;
    if (openCheck->isChecked()) result.options.userPassword  = openPwd->text();
    if (editCheck->isChecked()) result.options.ownerPassword = editPwd->text();
    result.options.allowPrinting   = !cPrint->isChecked();
    result.options.allowCopying    = !cCopy->isChecked();
    result.options.allowCommenting = !cComment->isChecked();
    result.options.allowAssembly   = !cAssemble->isChecked();
    result.options.allowFormFill   = !cForms->isChecked();
    return result;
}

} // namespace NativeOffice::Pdf
