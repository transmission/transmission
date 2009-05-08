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
        "Most of Transmission is covered by the MIT license."
        "\n\n"
        "Some files are copyrighted by Charles Kerr and are covered by "
        "the GPL version 2.  Works owned by the Transmission project "
        "are granted a special exemption to clause 2(b) so that the bulk "
        "of its code can remain under the MIT license.  This exemption does "
        "not extend to original or derived works not owned by the "
        "Transmission project. "
        "\n\n"
        "Permission is hereby granted, free of charge, to any person obtaining "
        "a copy of this software and associated documentation files (the "
        "'Software'), to deal in the Software without restriction, including "
        "without limitation the rights to use, copy, modify, merge, publish, "
        "distribute, sublicense, and/or sell copies of the Software, and to "
        "permit persons to whom the Software is furnished to do so, subject to "
        "the following conditions: "
        "\n\n"
        "The above copyright notice and this permission notice shall be included "
        "in all copies or substantial portions of the Software. "
        "\n\n"
        "THE SOFTWARE IS PROVIDED 'AS IS', WITHOUT WARRANTY OF ANY KIND, "
        "EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF "
        "MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. "
        "IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY "
        "CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, "
        "TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE "
        "SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE." );
    v->addWidget( t );

    QDialogButtonBox * box = new QDialogButtonBox;
    box->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
    box->setOrientation( Qt::Horizontal );
    box->setStandardButtons( QDialogButtonBox::Close );
    v->addWidget( box );

    connect( box, SIGNAL(rejected()), this, SLOT(hide()) );
}
