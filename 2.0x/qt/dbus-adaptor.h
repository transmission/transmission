/*
 * This file Copyright (C) 2010 Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id:$
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
    virtual ~TrDBusAdaptor() { }

  public slots:
    bool PresentWindow();
    bool AddMetainfo( const QString& payload, const QString& filename );
};

#endif
