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

#ifndef MAKE_DIALOG_H
#define MAKE_DIALOG_H

#include <QDialog>
#include <QTimer>

struct QAbstractButton;
struct QPlainTextEdit;
struct QLineEdit;
struct QCheckBox;
struct QLabel;
struct QProgressBar;
struct QPushButton;
struct Session;

extern "C"
{
    struct tr_metainfo_builder;
}

class MakeDialog: public QDialog
{
        Q_OBJECT

    private slots:
        void onFolderButtonPressed( );
        void onFileButtonPressed( );
        void onFileSelectedInDialog( const QString& path );
        void onSourceChanged( );
        void onButtonBoxClicked( QAbstractButton* );
        void onProgress( );
        void refresh( );

    private:
        void makeTorrent( );
        void refreshButtons( );
        void setIsBuilding( bool );
        QString getResult( ) const;


    private:
        QTimer myTimer;
        QLineEdit * mySourceEdit;
        QLabel * mySourceLabel;
        QPlainTextEdit * myTrackerEdit;
        QLineEdit * myCommentEdit;
        QCheckBox * myPrivateCheck;
        QProgressBar * myProgressBar;
        QLabel * myProgressLabel;
        QPushButton * myMakeButton;
        QPushButton * myCloseButton;
        QPushButton * myStopButton;
        struct tr_metainfo_builder * myBuilder;
        bool myIsBuilding;

    public:
        MakeDialog( Session&, QWidget * parent = 0 );
        ~MakeDialog( );
};

#endif
