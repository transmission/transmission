// This file Copyright © 2012-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <QVariant>

class CustomVariantType
{
public:
    enum
    {
        TrackerStatsList = QMetaType::User,
        PeerList,
        FileList,
        FilterModeType,
        SortModeType
    };
};
