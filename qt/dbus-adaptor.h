/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
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
    virtual ~TrDBusAdaptor() { }

  public slots:
    bool PresentWindow();
    bool AddMetainfo( const QString& );
};

#endif
