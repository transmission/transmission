// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <libtransmission/transmission.h>

#include <libtransmission-app/display-modes.h>

#include <QMetaType>

class UserMetaType
{
public:
    enum
    {
        ShowModeType = QMetaType::User,
        SortModeType,
        EncryptionModeType,
    };
};

Q_DECLARE_METATYPE(tr_encryption_mode)

using ShowMode = tr::app::ShowMode;
Q_DECLARE_METATYPE(ShowMode)
inline auto constexpr DefaultShowMode = tr::app::DefaultShowMode;
inline auto constexpr ShowModeCount = tr::app::ShowModeCount;

using SortMode = tr::app::SortMode;
Q_DECLARE_METATYPE(SortMode)
inline auto constexpr DefaultSortMode = tr::app::DefaultSortMode;
