/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <QApplication>
#include <QIcon>
#include <QMessageBox>
#include <QPushButton>

#include <libtransmission/transmission.h>
#include <libtransmission/version.h>

#include "AboutDialog.h"
#include "LicenseDialog.h"

AboutDialog::AboutDialog (QWidget * parent):
  QDialog (parent, Qt::Dialog),
  myLicenseDialog (new LicenseDialog (this))
{
  ui.setupUi (this);

  ui.iconLabel->setPixmap (qApp->windowIcon ().pixmap (48));
  ui.titleLabel->setText (tr ("<b style='font-size:x-large'>Transmission %1</b>").arg (QString::fromUtf8 (LONG_VERSION_STRING)));

  QPushButton * b;

  b = new QPushButton (tr ("C&redits"), this);
  ui.dialogButtons->addButton (b, QDialogButtonBox::ActionRole);
  connect (b, SIGNAL (clicked ()), this, SLOT (showCredits ()));

  b = new QPushButton (tr ("&License"), this);
  ui.dialogButtons->addButton (b, QDialogButtonBox::ActionRole);
  connect (b, SIGNAL (clicked ()), myLicenseDialog, SLOT (show ()));

  ui.dialogButtons->button (QDialogButtonBox::Close)->setDefault (true);
}

void
AboutDialog::showCredits ()
{
  QMessageBox::about (
    this,
    tr ("Credits"),
    QString::fromUtf8 ("Jordan Lee (Backend; Daemon; GTK+; Qt)\n"
                        "Michell Livingston (OS X)\n"));
}
