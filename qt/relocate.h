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

#ifndef RELOCATE_DIALOG_H
#define RELOCATE_DIALOG_H

#include <QDialog>
#include <QSet>
#include <QString>

class QPushButton;
class QRadioButton;
class Session;
class Torrent;
class TorrentModel;

class RelocateDialog: public QDialog
{
        Q_OBJECT

    private:
        QString myPath;
        static bool myMoveFlag;

    private:
        Session & mySession;
        TorrentModel& myModel;
        QSet<int> myIds;
        QPushButton * myDirButton;
        QRadioButton * myMoveRadio;

    private slots:
        void onFileSelected( const QString& path );
        void onDirButtonClicked( );
        void onSetLocation( );
        void onMoveToggled( bool );

    public:
        RelocateDialog( Session&, TorrentModel&, const QSet<int>& ids, QWidget * parent = 0 );
        ~RelocateDialog( ) { }
};

#endif
