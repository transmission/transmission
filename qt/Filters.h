/*
 * This file Copyright (C) 2010-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <QMetaType>
#include <QString>
#include <QVariant>

class FilterMode
{
public:
    enum
    {
        SHOW_ALL,
        SHOW_ACTIVE,
        SHOW_DOWNLOADING,
        SHOW_SEEDING,
        SHOW_PAUSED,
        SHOW_FINISHED,
        SHOW_VERIFYING,
        SHOW_ERROR,
        NUM_MODES
    };

public:
    FilterMode(int mode = SHOW_ALL) :
        mode_(mode)
    {
    }

    FilterMode(QString const& name) :
        mode_(modeFromName(name))
    {
    }

    int mode() const
    {
        return mode_;
    }

    QString const& name() const
    {
        return nameFromMode(mode_);
    }

    static int modeFromName(QString const& name);

    static QString const& nameFromMode(int mode)
    {
        return names_[mode];
    }

private:
    int mode_;

    static QString const names_[];
};

Q_DECLARE_METATYPE(FilterMode)

class SortMode
{
public:
    enum
    {
        SORT_BY_ACTIVITY,
        SORT_BY_AGE,
        SORT_BY_ETA,
        SORT_BY_NAME,
        SORT_BY_PROGRESS,
        SORT_BY_QUEUE,
        SORT_BY_RATIO,
        SORT_BY_SIZE,
        SORT_BY_STATE,
        SORT_BY_ID,
        NUM_MODES
    };

public:
    SortMode(int mode = SORT_BY_ID) :
        mode_(mode)
    {
    }

    SortMode(QString const& name) :
        mode_(modeFromName(name))
    {
    }

    int mode() const
    {
        return mode_;
    }

    QString const& name() const
    {
        return names_[mode_];
    }

    static int modeFromName(QString const& name);
    static QString const& nameFromMode(int mode);

private:
    int mode_ = SORT_BY_ID;

    static QString const names_[];
};

Q_DECLARE_METATYPE(SortMode)
