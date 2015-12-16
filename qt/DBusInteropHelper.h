/*
 * This file Copyright (C) 2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef QTR_DBUS_INTEROP_HELPER_H
#define QTR_DBUS_INTEROP_HELPER_H

class QObject;
class QString;
class QVariant;

class DBusInteropHelper
{
  public:
    bool isConnected () const;

    QVariant addMetainfo (const QString& metainfo);

    static void registerObject (QObject * parent);
};

#endif // QTR_DBUS_INTEROP_HELPER_H
