/*
 * This file Copyright (C) 2012-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef QTR_TYPES_H
#define QTR_TYPES_H

#include <QVariant>

namespace CustomVariantType
{
    enum CustomVariantType
    {
      TrackerStatsList = QVariant::UserType + 1,
      PeerList,
      FileList,
      FilterModeType,
      SortModeType
    };
};

#endif // QTR_TYPES_H
