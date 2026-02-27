// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <QTimer>

#include "ColumnResizer.h"
#include "Formatter.h"
#include "Session.h"
#include "StatsDialog.h"

namespace
{
int constexpr RefreshIntervalMsec = 15 * 1000;
}

StatsDialog::StatsDialog(Session& session, QWidget* parent)
    : BaseDialog{ parent }
    , session_{ session }
{
    ui_.setupUi(this);

    auto* cr = new ColumnResizer{ this };
    cr->add_layout(ui_.currentSessionSectionLayout);
    cr->add_layout(ui_.totalSectionLayout);
    cr->update();

    timer_.setSingleShot(false);
    connect(&timer_, &QTimer::timeout, &session_, &Session::refresh_session_stats);
    connect(&session_, &Session::stats_updated, this, &StatsDialog::update_stats);
    update_stats();
    session_.refresh_session_stats();
}

void StatsDialog::setVisible(bool visible)
{
    timer_.stop();

    if (visible)
    {
        timer_.start(RefreshIntervalMsec);
    }

    BaseDialog::setVisible(visible);
}

void StatsDialog::update_stats()
{
    tr_session_stats const& current(session_.get_stats());
    tr_session_stats const& total(session_.get_cumulative_stats());

    ui_.currentUploadedValueLabel->setText(Formatter::storage_to_string(current.uploadedBytes));
    ui_.currentDownloadedValueLabel->setText(Formatter::storage_to_string(current.downloadedBytes));
    ui_.currentRatioValueLabel->setText(Formatter::ratio_to_string(current.ratio));
    ui_.currentDurationValueLabel->setText(Formatter::time_to_string(current.secondsActive));

    ui_.totalUploadedValueLabel->setText(Formatter::storage_to_string(total.uploadedBytes));
    ui_.totalDownloadedValueLabel->setText(Formatter::storage_to_string(total.downloadedBytes));
    ui_.totalRatioValueLabel->setText(Formatter::ratio_to_string(total.ratio));
    ui_.totalDurationValueLabel->setText(Formatter::time_to_string(total.secondsActive));

    ui_.startCountLabel->setText(tr("Started %Ln time(s)", nullptr, total.sessionCount));
}
