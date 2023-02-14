// This file Copyright © 2012-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "AddData.h"
#include "Application.h"
#include "InteropObject.h"

InteropObject::InteropObject(QObject* parent)
    : QObject(parent)
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
    if (auto addme = AddData(metainfo); addme.type != addme.NONE)
    {
        trApp->addTorrent(addme);
    }

    return true;
}
