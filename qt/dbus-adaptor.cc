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

#include "app.h"
#include "dbus-adaptor.h"

TrDBusAdaptor :: TrDBusAdaptor( MyApp* app ):
    QDBusAbstractAdaptor( app ),
    myApp( app )
{
}

bool
TrDBusAdaptor :: PresentWindow( )
{
    myApp->raise( );
    return true;
}

bool
TrDBusAdaptor :: AddMetainfo( const QString& str )
{
    myApp->addTorrent( str );
    return true;
}
