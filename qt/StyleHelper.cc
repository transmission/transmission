/*
 * This file Copyright (C) 2017 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include "StyleHelper.h"

QIcon::Mode StyleHelper::getIconMode(QStyle::State state)
{
    if ((state & QStyle::State_Enabled) == 0)
    {
        return QIcon::Disabled;
    }

    if ((state & QStyle::State_Selected) != 0)
    {
        return QIcon::Selected;
    }

    return QIcon::Normal;
}
