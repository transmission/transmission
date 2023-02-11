// This file Copyright © 2017-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <QIcon>
#include <QStyle>

class QPainter;
class QStyleOptionProgressBar;

class StyleHelper
{
public:
    static QIcon::Mode getIconMode(QStyle::State const& state);

    static void drawProgressBar(QStyle const& style, QPainter& painter, QStyleOptionProgressBar const& option);
};
