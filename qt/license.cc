/*
 * This file Copyright (C) 2009-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <QDialogButtonBox>
#include <QPlainTextEdit>
#include <QVBoxLayout>

#include "license.h"

LicenseDialog::LicenseDialog (QWidget * parent):
  QDialog (parent, Qt::Dialog)
{
  setWindowTitle (tr ("License"));
  resize (400, 300);
  QVBoxLayout * v = new QVBoxLayout (this);

  QPlainTextEdit * t = new QPlainTextEdit (this);
  t->setReadOnly (true);
  t->setPlainText (
    "Copyright 2005-2014. All code is copyrighted by the respective authors.\n"
    "\n"
    "Transmission can be redistributed and/or modified under the terms of the "
    "GNU GPL versions 2 or 3 or by any future license endorsed by Mnemosyne LLC.\n"
    "\n"
    "In addition, linking to and/or using OpenSSL is allowed.\n"
    "\n"
    "This program is distributed in the hope that it will be useful, "
    "but WITHOUT ANY WARRANTY; without even the implied warranty of "
    "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n"
    "\n"
    "Some of Transmission's source files have more permissive licenses. "
    "Those files may, of course, be used on their own under their own terms.\n");
  v->addWidget (t);

  QDialogButtonBox * box = new QDialogButtonBox;
  box->setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Fixed);
  box->setOrientation (Qt::Horizontal);
  box->setStandardButtons (QDialogButtonBox::Close);
  v->addWidget (box);

  connect (box, SIGNAL (rejected ()), this, SLOT (hide ()));
}
