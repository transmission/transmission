/*
 * This file Copyright (C) 2009-2010 Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#ifndef ABOUT_DIALOG_H
#define ABOUT_DIALOG_H

#include <QDialog>

class AboutDialog: public QDialog
{
        Q_OBJECT

    private:
        QDialog * myLicenseDialog;

    public:
        AboutDialog( QWidget * parent = 0 );
        ~AboutDialog( ) { }
        QWidget * createAboutTab( );
        QWidget * createAuthorsTab( );
        QWidget * createLicenseTab( );

    public slots:
        void showCredits( );

};

#endif
