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
#include "Utils.h"

AboutDialog::AboutDialog (QWidget * parent):
  BaseDialog (parent),
  myLicenseDialog ()
{
  ui.setupUi (this);

  ui.iconLabel->setPixmap (qApp->windowIcon ().pixmap (48));
  ui.titleLabel->setText (tr ("<b style='font-size:x-large'>Transmission %1</b>").arg (QStringLiteral(LONG_VERSION_STRING)));

  QPushButton * b;

  b = ui.dialogButtons->addButton (tr ("C&redits"), QDialogButtonBox::ActionRole);
  connect (b, &QPushButton::clicked, [this]{
	  QMessageBox::about (
		this,
		tr ("Credits"),
		QString::fromUtf8 ("Jordan Lee (Backend; Daemon; GTK+; Qt)\n"
							"Michell Livingston (OS X)\n"));
  });

  b = ui.dialogButtons->addButton (tr ("&License"), QDialogButtonBox::ActionRole);
  connect (b, &QPushButton::clicked, [this]{  Utils::openDialog (myLicenseDialog, this); });
  ui.dialogButtons->button (QDialogButtonBox::Close)->setDefault (true);
}
