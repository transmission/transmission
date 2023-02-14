// This file Copyright © 2009-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <QApplication>
#include <QIcon>
#include <QMessageBox>
#include <QPushButton>

#include <libtransmission/transmission.h>
#include <libtransmission/version.h>

#include "AboutDialog.h"
#include "LicenseDialog.h"
#include "Session.h"
#include "Utils.h"

AboutDialog::AboutDialog(Session& session, QWidget* parent)
    : BaseDialog(parent)
{
    ui_.setupUi(this);

    ui_.iconLabel->setPixmap(QApplication::windowIcon().pixmap(48));

    if (session.isServer())
    {
        auto const title = QStringLiteral("<b style='font-size:x-large'>Transmission %1</b>")
                               .arg(QStringLiteral(LONG_VERSION_STRING));
        ui_.titleLabel->setText(title);
    }
    else
    {
        QString title = QStringLiteral(
            "<div style='font-size:x-large; font-weight: bold; text-align: center'>Transmission</div>");
        title += QStringLiteral("<div style='text-align: center'>%1: %2</div>")
                     .arg(tr("Client"))
                     .arg(QStringLiteral(LONG_VERSION_STRING));
        title += QStringLiteral("<div style='text-align: center'>%1: %2</div>").arg(tr("Server")).arg(session.sessionVersion());
        ui_.titleLabel->setText(title);
    }

    QPushButton const* b = ui_.dialogButtons->addButton(tr("C&redits"), QDialogButtonBox::ActionRole);
    connect(b, &QAbstractButton::clicked, this, &AboutDialog::showCredits);

    b = ui_.dialogButtons->addButton(tr("&License"), QDialogButtonBox::ActionRole);
    connect(b, &QAbstractButton::clicked, this, &AboutDialog::showLicense);

    ui_.dialogButtons->button(QDialogButtonBox::Close)->setDefault(true);
}

void AboutDialog::showCredits()
{
    QMessageBox::about(
        this,
        tr("Credits"),
        QString::fromUtf8("Charles Kerr (Backend; Daemon; GTK+; Qt)\n"
                          "Mitchell Livingston (macOS)\n"
                          "Mike Gelfand\n"));
}

void AboutDialog::showLicense()
{
    Utils::openDialog(license_dialog_, this); // NOLINT clang-analyzer-cplusplus.NewDelete
}
