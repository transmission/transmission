// This file Copyright © 2015-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <QVariant>

#include "InteropHelper.h"

bool InteropHelper::isConnected() const
{
#if defined(ENABLE_DBUS_INTEROP) && defined(ENABLE_COM_INTEROP)

    return dbus_client_.isConnected() || com_client_.isConnected();

#elif defined(ENABLE_DBUS_INTEROP)

    return dbus_client_.isConnected();

#elif defined(ENABLE_COM_INTEROP)

    return com_client_.isConnected();

#else

    return false;

#endif
}

bool InteropHelper::addMetainfo(QString const& metainfo) const
{
#if defined(ENABLE_DBUS_INTEROP) && defined(ENABLE_COM_INTEROP)

    return dbus_client_.addMetainfo(metainfo).toBool() || com_client_.addMetainfo(metainfo).toBool();

#elif defined(ENABLE_DBUS_INTEROP)

    return dbus_client_.addMetainfo(metainfo).toBool();

#elif defined(ENABLE_COM_INTEROP)

    return com_client_.addMetainfo(metainfo).toBool();

#else

    return false;

#endif
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
