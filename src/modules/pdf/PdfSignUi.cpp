// ─────────────────────────────────────────────────────────────────────────────
// PdfSignUi.cpp — see PdfSignUi.h.
// ─────────────────────────────────────────────────────────────────────────────
#include "PdfSignUi.h"
#include "core/theme/ThemeManager.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace NativeOffice::Pdf {

namespace {

QString fmtDate(const QDateTime& dt) {
    return dt.isValid() ? dt.toLocalTime().toString("yyyy-MM-dd") : QStringLiteral("—");
}

// Fills a combo with signing-capable certificates. Returns whether any exist.
bool populateSigningCombo(QComboBox* combo, std::vector<CertInfo>& certsOut) {
    certsOut = listCertificates();
    bool any = false;
    for (const CertInfo& c : certsOut) {
        if (!c.hasPrivateKey) continue;
        combo->addItem(QStringLiteral("%1  (expires %2)").arg(c.subject, fmtDate(c.notAfter)),
                       c.thumbprintHex);
        any = true;
    }
    return any;
}

} // namespace

void runCertificateManager(QWidget* parent) {
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Manage Certificates"));
    dlg.setStyleSheet(ThemeManager::inputDialogStyleSheet());
    dlg.resize(640, 360);
    auto* v = new QVBoxLayout(&dlg);
    v->addWidget(new QLabel(QObject::tr("Certificates in your Windows personal store (Current User \\ My):"), &dlg));

    const auto certs = listCertificates();
    auto* table = new QTableWidget(int(certs.size()), 5, &dlg);
    table->setHorizontalHeaderLabels({ QObject::tr("Subject"), QObject::tr("Issuer"),
        QObject::tr("Valid from"), QObject::tr("Valid to"), QObject::tr("Private key") });
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setStretchLastSection(true);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->verticalHeader()->setVisible(false);
    for (int i = 0; i < int(certs.size()); ++i) {
        const CertInfo& c = certs[size_t(i)];
        table->setItem(i, 0, new QTableWidgetItem(c.subject));
        table->setItem(i, 1, new QTableWidgetItem(c.issuer));
        table->setItem(i, 2, new QTableWidgetItem(fmtDate(c.notBefore)));
        table->setItem(i, 3, new QTableWidgetItem(fmtDate(c.notAfter)));
        table->setItem(i, 4, new QTableWidgetItem(c.hasPrivateKey ? QObject::tr("Yes") : QObject::tr("No")));
    }
    v->addWidget(table, 1);

    if (certs.empty())
        v->addWidget(new QLabel(QObject::tr("No certificates found. Install one via Windows "
                                            "(certmgr.msc) to sign documents."), &dlg));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    v->addWidget(buttons);
    dlg.exec();
}

SignDialogResult runSignDialog(QWidget* parent) {
    SignDialogResult result;

    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Certificate Signature"));
    dlg.setStyleSheet(ThemeManager::inputDialogStyleSheet());
    dlg.setMinimumWidth(440);
    auto* form = new QFormLayout(&dlg);

    auto* certCombo = new QComboBox(&dlg);
    std::vector<CertInfo> certs;
    const bool any = populateSigningCombo(certCombo, certs);
    form->addRow(QObject::tr("Certificate:"), certCombo);

    auto* reason = new QLineEdit(&dlg);
    reason->setPlaceholderText(QObject::tr("e.g. I approve this document"));
    auto* location = new QLineEdit(&dlg);
    auto* name = new QLineEdit(&dlg);
    name->setPlaceholderText(QObject::tr("(defaults to certificate name)"));
    form->addRow(QObject::tr("Reason:"), reason);
    form->addRow(QObject::tr("Location:"), location);
    form->addRow(QObject::tr("Signer name:"), name);

    if (!any) {
        auto* warn = new QLabel(QObject::tr("No signing certificate with a private key was found.\n"
            "Install one in the Windows personal store first."), &dlg);
        warn->setStyleSheet("color:#E8372A;");
        form->addRow(warn);
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    buttons->button(QDialogButtonBox::Ok)->setText(QObject::tr("Sign"));
    buttons->button(QDialogButtonBox::Ok)->setEnabled(any);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);

    if (dlg.exec() != QDialog::Accepted || !any) return result;

    result.confirmed = true;
    result.options.thumbprintHex = certCombo->currentData().toString();
    result.options.reason = reason->text();
    result.options.location = location->text();
    result.options.signerName = name->text();
    return result;
}

QString runTimestampDialog(QWidget* parent) {
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Add Timestamp"));
    dlg.setStyleSheet(ThemeManager::inputDialogStyleSheet());
    dlg.setMinimumWidth(420);
    auto* form = new QFormLayout(&dlg);

    auto* urlCombo = new QComboBox(&dlg);
    urlCombo->setEditable(true);
    urlCombo->addItems({
        QStringLiteral("http://timestamp.digicert.com"),
        QStringLiteral("http://timestamp.sectigo.com"),
        QStringLiteral("http://tsa.starfieldtech.com"),
    });
    form->addRow(QObject::tr("TSA URL:"), urlCombo);
    auto* note = new QLabel(QObject::tr("A trusted Time Stamping Authority certifies when the "
                                        "document was signed. This requires an internet connection."), &dlg);
    note->setWordWrap(true);
    note->setStyleSheet("color:#9AA4B8; font-size:8pt;");
    form->addRow(note);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    buttons->button(QDialogButtonBox::Ok)->setText(QObject::tr("Timestamp"));
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);

    if (dlg.exec() != QDialog::Accepted) return {};
    return urlCombo->currentText().trimmed();
}

void showValidationReport(QWidget* parent, const std::vector<SignatureStatus>& results) {
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Validate Signature"));
    dlg.setStyleSheet(ThemeManager::inputDialogStyleSheet());
    dlg.setMinimumWidth(480);
    auto* v = new QVBoxLayout(&dlg);

    if (results.empty()) {
        v->addWidget(new QLabel(QObject::tr("This document contains no digital signatures."), &dlg));
    } else {
        for (const SignatureStatus& s : results) {
            auto* box = new QLabel(&dlg);
            const QString icon = s.digestValid && s.certTrusted ? "✓"
                               : s.digestValid ? "!" : "✕";
            const QString color = s.digestValid && s.certTrusted ? "#16A34A"
                                : s.digestValid ? "#EA580C" : "#E8372A";
            QString body = QStringLiteral("<b style='color:%1'>%2 %3</b>")
                .arg(color, icon, s.isTimestamp ? QObject::tr("Timestamp") : QObject::tr("Signature"));
            if (!s.signerName.isEmpty()) body += QStringLiteral("<br>%1: %2")
                .arg(QObject::tr("Signer"), s.signerName.toHtmlEscaped());
            if (!s.reason.isEmpty()) body += QStringLiteral("<br>%1: %2")
                .arg(QObject::tr("Reason"), s.reason.toHtmlEscaped());
            if (s.signedAt.isValid()) body += QStringLiteral("<br>%1: %2")
                .arg(QObject::tr("Time"), s.signedAt.toLocalTime().toString(Qt::TextDate));
            body += QStringLiteral("<br>%1").arg(s.summary);
            box->setText(body);
            box->setTextFormat(Qt::RichText);
            box->setWordWrap(true);
            box->setStyleSheet("padding:8px; border:1px solid #2A2F3A; border-radius:6px;");
            v->addWidget(box);
        }
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    v->addWidget(buttons);
    dlg.exec();
}

} // namespace NativeOffice::Pdf
