/*
 * This file Copyright (C) 2009 Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#include <QDialogButtonBox>
#include <QFont>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QString>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>

#include <libtransmission/transmission.h>
#include <libtransmission/version.h>

#include "about.h"
#include "hig.h"
#include "license.h"

AboutDialog :: AboutDialog( QWidget * parent ):
    QDialog( parent, Qt::Dialog ),
    myLicenseDialog( new LicenseDialog( this ) )
{
    setWindowTitle( tr( "About Transmission" ) );
    QLabel * l;
    QVBoxLayout * v = new QVBoxLayout( this );

    l = new QLabel;
    l->setPixmap( QPixmap( ":/icons/transmission-48.png" ) );
    l->setAlignment( Qt::AlignCenter );
    v->addWidget( l );

    QFont f( font( ) );
    f.setWeight( QFont::Bold );
    f.setPointSize( int( f.pointSize( ) * 1.2 ) );
    l = new QLabel( "<big>Transmission " LONG_VERSION_STRING "</big>" );
    l->setAlignment( Qt::AlignCenter );
    l->setFont( f );
    l->setMargin( 8 );
    v->addWidget( l );

    l = new QLabel( tr( "A fast and easy BitTorrent client" ) );
    l->setStyleSheet( "text-align: center" );
    l->setAlignment( Qt::AlignCenter );
    v->addWidget( l );

    l = new QLabel( tr( "Copyright 2005-2009 The Transmission Project" ) );
    l->setAlignment( Qt::AlignCenter );
    v->addWidget( l );

    l = new QLabel( "<a href=\"http://www.transmissionbt.com/\">http://www.transmissionbt.com/</a>" );
    l->setOpenExternalLinks( true );
    l->setAlignment( Qt::AlignCenter );
    v->addWidget( l );

    v->addSpacing( HIG::PAD_BIG );

    QPushButton * b;
    QDialogButtonBox * box = new QDialogButtonBox;

    b = new QPushButton( tr( "C&redits" ), this );
    box->addButton( b, QDialogButtonBox::ActionRole );
    connect( b, SIGNAL(clicked()), this, SLOT(showCredits()) );

    b = new QPushButton( tr( "&License" ), this );
    box->addButton( b, QDialogButtonBox::ActionRole );
    connect( b, SIGNAL(clicked()), myLicenseDialog, SLOT(show()) );

    box->addButton( QDialogButtonBox::Close );
    box->setCenterButtons( true );
    v->addWidget( box );
    connect( box, SIGNAL(rejected()), this, SLOT(hide()) );
}

void
AboutDialog :: showCredits( )
{
    QMessageBox::about( this, tr( "Credits" ),
        "Charles Kerr (Backend; Daemon; GTK+; Qt)\n"
        "Michell Livingston (Backend; OS X)\n"
        "Kevin Glowacz (Web client)" );
}

