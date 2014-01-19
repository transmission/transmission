/*
 * This file Copyright (C) 2012-2014 Mnemosyne LLC
 *
 * It may be used under the GNU Public License v2 or v3 licenses,
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef QTR_DBUS_ADAPTOR_H
#define QTR_DBUS_ADAPTOR_H

class MyApp;

#include <QDBusAbstractAdaptor>

class TrDBusAdaptor: public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO( "D-Bus Interface", "com.transmissionbt.Transmission" )

  private:
    MyApp * myApp;

  public:
    TrDBusAdaptor( MyApp* );
    virtual ~TrDBusAdaptor() {}

  public slots:
    bool PresentWindow();
    bool AddMetainfo( const QString& );
};

#endif
