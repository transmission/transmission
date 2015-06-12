/*
 * This file Copyright (C) 2012-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef QTR_DBUS_ADAPTOR_H
#define QTR_DBUS_ADAPTOR_H

#include <QDBusAbstractAdaptor>

class Application;

class DBusAdaptor: public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO ("D-Bus Interface", "com.transmissionbt.Transmission")

  public:
    DBusAdaptor (Application *);
    virtual ~DBusAdaptor () {}

  public slots:
    bool PresentWindow ();
    bool AddMetainfo (const QString&);

  private:
    Application * myApp;
};

#endif // QTR_DBUS_ADAPTOR_H
