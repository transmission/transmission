// This file Copyright © 2015-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <QVariant>

#include "InteropHelper.h"

bool InteropHelper::isConnected() const
{
    bool is_connected = false;

#ifdef ENABLE_DBUS_INTEROP

    is_connected |= dbus_client_.isConnected();

#endif

#ifdef ENABLE_COM_INTEROP

    is_connected |= com_client_.isConnected();

#endif

    return is_connected;
}

bool InteropHelper::addMetainfo(QString const& metainfo) const
{
#ifdef ENABLE_DBUS_INTEROP

    if (auto const response = dbus_client_.addMetainfo(metainfo); response.isValid() && response.toBool())
    {
        return true;
    }

#endif

#ifdef ENABLE_COM_INTEROP

    if (const response = com_client_.addMetainfo(metainfo); response.isValid() && response.toBool())
    {
        return true;
    }

#endif

    return false;
}

void InteropHelper::initialize()
{
#ifdef ENABLE_COM_INTEROP
    ComInteropHelper::initialize();
#endif
}

void InteropHelper::registerObject(QObject* parent)
{
#ifdef ENABLE_DBUS_INTEROP
    DBusInteropHelper::registerObject(parent);
#endif

#ifdef ENABLE_COM_INTEROP
    ComInteropHelper::registerObject(parent);
#endif
}
