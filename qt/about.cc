/*
 * This file Copyright (C) 2009 Charles Kerr <charles@transmissionbt.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id:$
 */

#include <QAbstractButton>
#include <QMovie>

#include <libtransmission/transmission.h>
#include <libtransmission/version.h>

#include "about.h"

namespace
{
    QMovie * movie;
}

AboutDialog :: AboutDialog( QWidget * parent ):
    QDialog( parent, Qt::Dialog )
{
    ui.setupUi( this );

    ui.label->setText( "Transmission " LONG_VERSION_STRING );
    connect( ui.buttonBox, SIGNAL(clicked(QAbstractButton*)), this, SLOT(hide()));
    movie = new QMovie( ":/icons/dance.gif" );
    connect( movie, SIGNAL(frameChanged(int)), this, SLOT(onFrameChanged()));
    movie->start( );
}

AboutDialog :: ~AboutDialog( )
{
}

void
AboutDialog :: onFrameChanged( )
{
    ui.aboutLogo->setPixmap( movie->currentPixmap( ) );
}
