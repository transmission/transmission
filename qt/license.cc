/*
 * This file Copyright (C) 2009 Charles Kerr <charles@transmissionbt.com>
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
#include <QPlainTextEdit>
#include <QVBoxLayout>

#include "license.h"

LicenseDialog :: LicenseDialog( QWidget * parent ):
    QDialog( parent, Qt::Dialog )
{
    setWindowTitle( tr( "License" ) );
    resize( 400, 300 );
    QVBoxLayout * v = new QVBoxLayout( this );

    QPlainTextEdit * t = new QPlainTextEdit( this );
    t->setReadOnly( true );
    t->setPlainText( 
"The OS X client, CLI client, and parts of libtransmission are licensed under the terms of the MIT license.\n\n"
"The Transmission daemon, GTK+ client, Qt client, Web client, and most of libtransmission are licensed under the terms of the GNU GPL version 2, with two special exceptions:\n\n"
"1. The MIT-licensed portions of Transmission listed above are exempt from GPLv2 clause 2(b) and may retain their MIT license.\n\n"
"2. Permission is granted to link the code in this release with the OpenSSL project's 'OpenSSL' library and to distribute the linked executables.  Works derived from Transmission may, at their authors' discretion, keep or delete this exception." );
    v->addWidget( t );

    QDialogButtonBox * box = new QDialogButtonBox;
    box->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
    box->setOrientation( Qt::Horizontal );
    box->setStandardButtons( QDialogButtonBox::Close );
    v->addWidget( box );

    connect( box, SIGNAL(rejected()), this, SLOT(hide()) );
}
