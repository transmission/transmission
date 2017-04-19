/*
 * This file Copyright (C) 2017 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <QIcon>
#include <QStyle>

class StyleHelper
{
public:
    static QIcon::Mode getIconMode(QStyle::State state);
};
