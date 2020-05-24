/*
 * This file Copyright (C) 2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <QVariant>

#include "InteropHelper.h"

bool InteropHelper::isConnected() const
{
#ifdef ENABLE_DBUS_INTEROP

    if (dbus_client_.isConnected())
    {
        return true;
    }

#endif

#ifdef ENABLE_COM_INTEROP

    if (com_client_.isConnected())
    {
        return true;
    }

#endif

    return false;
}

bool InteropHelper::addMetainfo(QString const& metainfo)
{
#ifdef ENABLE_DBUS_INTEROP

    {
        QVariant const response = dbus_client_.addMetainfo(metainfo);

        if (response.isValid() && response.toBool())
        {
            return true;
        }
    }

#endif

#ifdef ENABLE_COM_INTEROP

    {
        QVariant const response = com_client_.addMetainfo(metainfo);

        if (response.isValid() && response.toBool())
        {
            return true;
        }
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
