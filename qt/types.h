/*
 * This file Copyright (C) 2009 Charles Kerr <charles@transmissionbt.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#ifndef QTR_TYPES_H
#define QTR_TYPES_H

#include <QVariant>

class TrTypes
{
    public:

        enum
        {
            PeerList = QVariant::UserType,
            FileList,
            FilterModeType,
            SortModeType
        };
};

#endif
