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

#ifndef MAKE_DIALOG_H
#define MAKE_DIALOG_H

#include <QDialog>
#include <QTimer>

struct QAbstractButton;
struct QPlainTextEdit;
struct QLineEdit;
struct QCheckBox;
struct QLabel;
struct QPushButton;
struct QRadioButton;
struct Session;
struct QProgressBar;
struct QDialogButtonBox;

extern "C"
{
    struct tr_metainfo_builder;
}

class MakeDialog: public QDialog
{
        Q_OBJECT

    private slots:
        void onSourceChanged( );
        void onButtonBoxClicked( QAbstractButton* );
        void onNewButtonBoxClicked( QAbstractButton* );
        void onNewDialogDestroyed( QObject* );
        void onProgress( );

        void onFolderClicked( );
        void onFolderSelected( const QStringList& );
        void onFileClicked( );
        void onFileSelected( const QStringList& );
        void onDestinationClicked( );
        void onDestinationSelected( const QStringList& );

    private:
        void makeTorrent( );
        QString getSource( ) const;
        void enableBuddyWhenChecked( QCheckBox *, QWidget * );
        void enableBuddyWhenChecked( QRadioButton *, QWidget * );

    private:
        Session& mySession;
        QString myDestination;
        QString myTarget;
        QString myFile;
        QString myFolder;
        QTimer myTimer;
        QRadioButton * myFolderRadio;
        QRadioButton * myFileRadio;
        QPushButton * myDestinationButton;
        QPushButton * myFileButton;
        QPushButton * myFolderButton;
        QPlainTextEdit * myTrackerEdit;
        QCheckBox * myCommentCheck;
        QLineEdit * myCommentEdit;
        QCheckBox * myPrivateCheck;
        QLabel * mySourceLabel;
        QDialogButtonBox * myButtonBox;
        QProgressBar * myNewProgress;
        QLabel * myNewLabel;
        QDialogButtonBox * myNewButtonBox;
        QDialog * myNewDialog;
        struct tr_metainfo_builder * myBuilder;

    public:
        MakeDialog( Session&, QWidget * parent = 0 );
        ~MakeDialog( );
};

#endif
