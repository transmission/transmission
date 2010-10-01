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

#include "add-data.h"
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
TrDBusAdaptor :: AddMetainfo( const QString& key )
{
    AddData addme( key );

    if( addme.type != addme.NONE )
        myApp->addTorrent( addme );

    return true;
}
