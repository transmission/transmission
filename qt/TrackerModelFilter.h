/*
 * This file Copyright (C) 2010-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <QSortFilterProxyModel>

class TrackerModelFilter : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    TrackerModelFilter(QObject* parent = nullptr);

    void setShowBackupTrackers(bool);

    bool showBackupTrackers() const
    {
        return show_backups_;
    }

protected:
    // QSortFilterProxyModel
    virtual bool filterAcceptsRow(int source_row, QModelIndex const& source_parent) const;

private:
    bool show_backups_ = {};
};
