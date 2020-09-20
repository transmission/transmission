/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <QTimer>

#include "ColumnResizer.h"
#include "Formatter.h"
#include "Session.h"
#include "StatsDialog.h"

enum
{
    REFRESH_INTERVAL_MSEC = (15 * 1000)
};

StatsDialog::StatsDialog(Session& session, QWidget* parent) :
    BaseDialog(parent),
    session_(session),
    timer_(new QTimer(this))
{
    ui_.setupUi(this);

    auto* cr = new ColumnResizer(this);
    cr->addLayout(ui_.currentSessionSectionLayout);
    cr->addLayout(ui_.totalSectionLayout);
    cr->update();

    timer_->setSingleShot(false);
    connect(timer_, SIGNAL(timeout()), &session_, SLOT(refreshSessionStats()));

    connect(&session_, SIGNAL(statsUpdated()), this, SLOT(updateStats()));
    updateStats();
    session_.refreshSessionStats();
}

void StatsDialog::setVisible(bool visible)
{
    timer_->stop();

    if (visible)
    {
        timer_->start(REFRESH_INTERVAL_MSEC);
    }

    BaseDialog::setVisible(visible);
}

void StatsDialog::updateStats()
{
    tr_session_stats const& current(session_.getStats());
    tr_session_stats const& total(session_.getCumulativeStats());

    ui_.currentUploadedValueLabel->setText(Formatter::get().sizeToString(current.uploadedBytes));
    ui_.currentDownloadedValueLabel->setText(Formatter::get().sizeToString(current.downloadedBytes));
    ui_.currentRatioValueLabel->setText(Formatter::get().ratioToString(current.ratio));
    ui_.currentDurationValueLabel->setText(Formatter::get().timeToString(current.secondsActive));

    ui_.totalUploadedValueLabel->setText(Formatter::get().sizeToString(total.uploadedBytes));
    ui_.totalDownloadedValueLabel->setText(Formatter::get().sizeToString(total.downloadedBytes));
    ui_.totalRatioValueLabel->setText(Formatter::get().ratioToString(total.ratio));
    ui_.totalDurationValueLabel->setText(Formatter::get().timeToString(total.secondsActive));

    ui_.startCountLabel->setText(tr("Started %Ln time(s)", nullptr, total.sessionCount));
}
