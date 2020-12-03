/*
 * This file Copyright (C) 2012-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include "AddData.h"
#include "Application.h"
#include "InteropObject.h"

InteropObject::InteropObject(QObject* parent) :
    QObject(parent)
{
}

// NOLINTNEXTLINE(readability-identifier-naming)
bool InteropObject::PresentWindow() const
{
    trApp->raise();
    return true;
}

// NOLINTNEXTLINE(readability-identifier-naming)
bool InteropObject::AddMetainfo(QString const& metainfo) const
{
    AddData addme(metainfo);

    if (addme.type != addme.NONE)
    {
        trApp->addTorrent(addme);
    }

    return true;
}
