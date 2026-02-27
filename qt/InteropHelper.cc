// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <QVariant>

#include "InteropHelper.h"

bool InteropHelper::is_connected() const
{
#if defined(ENABLE_DBUS_INTEROP) && defined(ENABLE_COM_INTEROP)

    return dbus_client_.is_connected() || com_client_.isConnected();

#elif defined(ENABLE_DBUS_INTEROP)

    return dbus_client_.is_connected();

#elif defined(ENABLE_COM_INTEROP)

    return com_client_.isConnected();

#else

    return false;

#endif
}

bool InteropHelper::add_metainfo(QString const& metainfo) const
{
#if defined(ENABLE_DBUS_INTEROP) && defined(ENABLE_COM_INTEROP)

    return dbus_client_.add_metainfo(metainfo).toBool() || com_client_.add_metainfo(metainfo).toBool();

#elif defined(ENABLE_DBUS_INTEROP)

    return dbus_client_.add_metainfo(metainfo).toBool();

#elif defined(ENABLE_COM_INTEROP)

    return com_client_.add_metainfo(metainfo).toBool();

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

void InteropHelper::register_object(QObject* parent)
{
#ifdef ENABLE_DBUS_INTEROP
    DBusInteropHelper::register_object(parent);
#endif

#ifdef ENABLE_COM_INTEROP
    ComInteropHelper::register_object(parent);
#endif
}
