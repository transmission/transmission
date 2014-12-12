/*
 * This file Copyright (C) 2009-2014 Mnemosyne LLC
 *
 * It may be used under the GNU Public License v2 or v3 licenses,
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <QDialogButtonBox>
#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>

#include "formatter.h"
#include "hig.h"
#include "session.h"
#include "stats-dialog.h"

enum
{
  REFRESH_INTERVAL_MSEC = (15*1000)
};

StatsDialog::StatsDialog (Session& session, QWidget * parent):
  QDialog (parent, Qt::Dialog),
  mySession (session),
  myTimer (new QTimer (this))
{
  myTimer->setSingleShot (false);
  connect (myTimer, SIGNAL (timeout ()), this, SLOT (onTimer ()));
  setWindowTitle (tr ("Statistics"));

  HIG * hig = new HIG ();
  hig->addSectionTitle (tr ("Current Session"));
  hig->addRow (tr ("Uploaded:"), myCurrentUp = new QLabel ());
  hig->addRow (tr ("Downloaded:"), myCurrentDown = new QLabel ());
  hig->addRow (tr ("Ratio:"), myCurrentRatio = new QLabel ());
  hig->addRow (tr ("Duration:"), myCurrentDuration = new QLabel ());
  hig->addSectionDivider ();
  hig->addSectionTitle (tr ("Total"));
  hig->addRow (myStartCount = new QLabel (tr ("Started %n time (s)", 0, 1)), 0);
  hig->addRow (tr ("Uploaded:"), myTotalUp = new QLabel ());
  hig->addRow (tr ("Downloaded:"), myTotalDown = new QLabel ());
  hig->addRow (tr ("Ratio:"), myTotalRatio = new QLabel ());
  hig->addRow (tr ("Duration:"), myTotalDuration = new QLabel ());
  hig->finish ();

  QLayout * layout = new QVBoxLayout (this);
  layout->addWidget (hig);
  QDialogButtonBox * buttons = new QDialogButtonBox (QDialogButtonBox::Close, Qt::Horizontal, this);
  connect (buttons, SIGNAL (rejected ()), this, SLOT (hide ())); // "close" triggers rejected
  layout->addWidget (buttons);

  connect (&mySession, SIGNAL (statsUpdated ()), this, SLOT (updateStats ()));
  updateStats ();
  mySession.refreshSessionStats ();
}

StatsDialog::~StatsDialog ()
{
}

void
StatsDialog::setVisible (bool visible)
{
  myTimer->stop ();
  if (visible)
    myTimer->start (REFRESH_INTERVAL_MSEC);
  QDialog::setVisible (visible);
}

void
StatsDialog::onTimer ()
{
  mySession.refreshSessionStats ();
}

void
StatsDialog::updateStats ()
{
  const struct tr_session_stats& current (mySession.getStats ());
  const struct tr_session_stats& total (mySession.getCumulativeStats ());

  myCurrentUp->setText (Formatter::sizeToString (current.uploadedBytes));
  myCurrentDown->setText (Formatter::sizeToString (current.downloadedBytes));
  myCurrentRatio->setText (Formatter::ratioToString (current.ratio));
  myCurrentDuration->setText (Formatter::timeToString (current.secondsActive));

  myTotalUp->setText (Formatter::sizeToString (total.uploadedBytes));
  myTotalDown->setText (Formatter::sizeToString (total.downloadedBytes));
  myTotalRatio->setText (Formatter::ratioToString (total.ratio));
  myTotalDuration->setText (Formatter::timeToString (total.secondsActive));

  myStartCount->setText (tr ("Started %n time (s)", 0, total.sessionCount));
}
