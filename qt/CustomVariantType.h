/*
 * This file Copyright (C) 2012-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <QVariant>

class CustomVariantType
{
public:
    enum
    {
        TrackerStatsList = QVariant::UserType,
        PeerList = QVariant::UserType,
        FileList,
        FilterModeType,
        SortModeType
    };
};
