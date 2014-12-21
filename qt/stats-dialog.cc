/*
 * This file Copyright (C) 2009-2014 Mnemosyne LLC
 *
 * It may be used under the GNU Public License v2 or v3 licenses,
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <QTimer>

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
  ui.setupUi (this);

  myTimer->setSingleShot (false);
  connect (myTimer, SIGNAL (timeout ()), &mySession, SLOT (refreshSessionStats ()));

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
StatsDialog::updateStats ()
{
  const tr_session_stats& current (mySession.getStats ());
  const tr_session_stats& total (mySession.getCumulativeStats ());

  ui.currentUploadedValueLabel->setText (Formatter::sizeToString (current.uploadedBytes));
  ui.currentDownloadedValueLabel->setText (Formatter::sizeToString (current.downloadedBytes));
  ui.currentRatioValueLabel->setText (Formatter::ratioToString (current.ratio));
  ui.currentDurationValueLabel->setText (Formatter::timeToString (current.secondsActive));

  ui.totalUploadedValueLabel->setText (Formatter::sizeToString (total.uploadedBytes));
  ui.totalDownloadedValueLabel->setText (Formatter::sizeToString (total.downloadedBytes));
  ui.totalRatioValueLabel->setText (Formatter::ratioToString (total.ratio));
  ui.totalDurationValueLabel->setText (Formatter::timeToString (total.secondsActive));

  ui.startCountLabel->setText (tr ("Started %n time(s)", 0, total.sessionCount));
}
