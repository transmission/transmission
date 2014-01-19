/*
 * This file Copyright (C) 2009-2014 Mnemosyne LLC
 *
 * It may be used under the GNU Public License v2 or v3 licenses,
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <QDialogButtonBox>
#include <QPlainTextEdit>
#include <QVBoxLayout>

#include "license.h"

LicenseDialog :: LicenseDialog (QWidget * parent):
  QDialog (parent, Qt::Dialog)
{
  setWindowTitle (tr ("License"));
  resize (400, 300);
  QVBoxLayout * v = new QVBoxLayout (this);

  QPlainTextEdit * t = new QPlainTextEdit (this);
  t->setReadOnly (true);
  t->setPlainText (
"Transmission may be used under the GNU Public License v2 or v3 licenses, or any future license endorsed by Mnemosyne LLC.\n \nPermission is granted to link the code in this release with the OpenSSL project's 'OpenSSL' library and to distribute the linked executables.  Works derived from Transmission may, at their authors' discretion, keep or delete this exception.");
  v->addWidget (t);

  QDialogButtonBox * box = new QDialogButtonBox;
  box->setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Fixed);
  box->setOrientation (Qt::Horizontal);
  box->setStandardButtons (QDialogButtonBox::Close);
  v->addWidget (box);

  connect (box, SIGNAL (rejected ()), this, SLOT (hide ()));
}
