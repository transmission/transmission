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

#ifndef ABOUT_DIALOG_H
#define ABOUT_DIALOG_H

class QAbstractButton;
class QIcon;
#include <QDialog>

#include "ui_about.h"

class AboutDialog: public QDialog
{
        Q_OBJECT

    private:
        Ui_AboutDialog ui;

    private slots: 
        void onFrameChanged( );

    public:
        AboutDialog( QWidget * parent = 0 );
        ~AboutDialog( );

};

#endif
