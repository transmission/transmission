/*
 * This file Copyright (C) 2009-2014 Mnemosyne LLC
 *
 * It may be used under the GNU Public License v2 or v3 licenses,
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "hig.h"
#include "prefs.h"
#include "session.h"
#include "session-dialog.h"

/***
****
***/

void
SessionDialog::onAccepted ()
{
  myPrefs.set (Prefs::SESSION_IS_REMOTE, myRemoteRadioButton->isChecked ());
  myPrefs.set (Prefs::SESSION_REMOTE_HOST, myHostLineEdit->text ());
  myPrefs.set (Prefs::SESSION_REMOTE_PORT, myPortSpinBox->value ());
  myPrefs.set (Prefs::SESSION_REMOTE_AUTH, myAuthCheckBox->isChecked ());
  myPrefs.set (Prefs::SESSION_REMOTE_USERNAME, myUsernameLineEdit->text ());
  myPrefs.set (Prefs::SESSION_REMOTE_PASSWORD, myPasswordLineEdit->text ());
  mySession.restart ();
  hide ();
}

void
SessionDialog::resensitize ()
{
  const bool isRemote = myRemoteRadioButton->isChecked();
  const bool useAuth = myAuthCheckBox->isChecked();

  foreach (QWidget * w, myRemoteWidgets)
    w->setEnabled (isRemote);

  foreach (QWidget * w, myAuthWidgets)
    w->setEnabled (isRemote && useAuth);
}

/***
****
***/

SessionDialog::SessionDialog (Session& session, Prefs& prefs, QWidget * parent):
  QDialog (parent),
  mySession (session),
  myPrefs (prefs)
{
  QWidget * l;
  QSpinBox * sb;
  QCheckBox * cb;
  QLineEdit * le;
  QRadioButton * rb;

  setWindowTitle (tr ("Change Session"));
  QVBoxLayout * top = new QVBoxLayout (this);
  top->setSpacing (HIG::PAD);

  HIG * hig = new HIG;
  hig->setContentsMargins (0, 0, 0, 0);
  hig->addSectionTitle (tr ("Source"));
  rb = new QRadioButton (tr ("Start &Local Session"));
  rb->setChecked (!prefs.get<bool>(Prefs::SESSION_IS_REMOTE));
  connect (rb, SIGNAL(toggled(bool)), this, SLOT(resensitize()));
  hig->addWideControl (rb);
  rb = myRemoteRadioButton = new QRadioButton (tr ("Connect to &Remote Session"));
  rb->setChecked (prefs.get<bool>(Prefs::SESSION_IS_REMOTE));
  connect (rb, SIGNAL(toggled(bool)), this, SLOT(resensitize()));
  hig->addWideControl (rb);
  le = myHostLineEdit = new QLineEdit ();
  le->setText (prefs.get<QString>(Prefs::SESSION_REMOTE_HOST));
  l = hig->addRow (tr ("&Host:"), le);
  myRemoteWidgets << l << le;
  sb = myPortSpinBox = new QSpinBox;
  sb->setRange (1, 65535);
  sb->setValue (prefs.get<int>(Prefs::SESSION_REMOTE_PORT));
  l = hig->addRow (tr ("&Port:"), sb);
  myRemoteWidgets << l << sb;
  cb = myAuthCheckBox = new QCheckBox (tr ("&Authentication required"));
  cb->setChecked (prefs.get<bool>(Prefs::SESSION_REMOTE_AUTH));
  connect (cb, SIGNAL(toggled(bool)), this, SLOT(resensitize()));
  myRemoteWidgets << cb;
  hig->addWideControl (cb);
  le = myUsernameLineEdit = new QLineEdit ();
  le->setText (prefs.get<QString>(Prefs::SESSION_REMOTE_USERNAME));
  l = hig->addRow (tr ("&Username:"), le);
  myAuthWidgets << l << le;
  le = myPasswordLineEdit = new QLineEdit ();
  le->setEchoMode (QLineEdit::Password);
  le->setText (prefs.get<QString>(Prefs::SESSION_REMOTE_PASSWORD));
  l = hig->addRow (tr ("Pass&word:"), le);
  myAuthWidgets << l << le;
  hig->finish ();
  top->addWidget (hig, 1);
  resensitize ();

  QDialogButtonBox * buttons = new QDialogButtonBox (QDialogButtonBox::Cancel|QDialogButtonBox::Ok);
  connect (buttons, SIGNAL(rejected()), this, SLOT(hide()));
  connect (buttons, SIGNAL(accepted()), this, SLOT(onAccepted()));
  top->addWidget (buttons, 0);
}
