/*
 * This file Copyright (C) 2012-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef QTR_INTEROP_OBJECT_H
#define QTR_INTEROP_OBJECT_H

#include <QObject>

class InteropObject: public QObject
{
    Q_OBJECT

#ifdef ENABLE_DBUS_INTEROP
    Q_CLASSINFO ("D-Bus Interface", "com.transmissionbt.Transmission")
#endif

#ifdef ENABLE_COM_INTEROP
    Q_CLASSINFO ("ClassID", "{0e2c952c-0597-491f-ba26-249d7e6fab49}")
    Q_CLASSINFO ("InterfaceID", "{9402f54f-4906-4f20-ad73-afcfeb5b228d}")
    Q_CLASSINFO ("RegisterObject", "yes")
    Q_CLASSINFO ("CoClassAlias", "QtClient")
    Q_CLASSINFO ("Description", "Transmission Qt Client Class")
#endif

  public:
    InteropObject (QObject * parent = nullptr);

  public slots:
    bool PresentWindow ();
    bool AddMetainfo (const QString& metainfo);
};

#endif // QTR_INTEROP_OBJECT_H
