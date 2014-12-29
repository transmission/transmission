/*
 * This file Copyright (C) 2009-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef OPTIONS_DIALOG_H
#define OPTIONS_DIALOG_H

#include <QCryptographicHash>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QMap>
#include <QString>
#include <QString>
#include <QTimer>
#include <QVector>

#include "add-data.h" // AddData
#include "file-tree.h" // FileList

#include "ui_options.h"

class Prefs;
class Session;

extern "C"
{
  struct tr_variant;
}

class OptionsDialog: public QDialog
{
    Q_OBJECT

  public:
    OptionsDialog (Session& session, const Prefs& prefs, const AddData& addme, QWidget * parent = 0);
    ~OptionsDialog ();

  private:
    void reload ();
    void clearInfo ();
    void clearVerify ();

  private slots:
    void onAccepted ();
    void onPriorityChanged (const QSet<int>& fileIndices, int);
    void onWantedChanged (const QSet<int>& fileIndices, bool);
    void onVerify ();
    void onTimeout ();

    void onSourceClicked ();
    void onSourceSelected (const QString&);
    void onSourceEdited ();

    void onDestinationClicked ();
    void onDestinationSelected (const QString&);
    void onDestinationEdited ();

  private:
    Session& mySession;
    AddData myAdd;
    QDir myLocalDestination;
    bool myHaveInfo;
    tr_info myInfo;
    Ui::OptionsDialog ui;
    QPushButton * myVerifyButton;
    QVector<int> myPriorities;
    QVector<bool> myWanted;
    FileList myFiles;

  private:
    QTimer myVerifyTimer;
    char myVerifyBuf[2048 * 4];
    QFile myVerifyFile;
    uint64_t myVerifyFilePos;
    int myVerifyFileIndex;
    uint32_t myVerifyPieceIndex;
    uint32_t myVerifyPiecePos;
    QVector<bool> myVerifyFlags;
    QCryptographicHash myVerifyHash;
    typedef QMap<uint32_t, int32_t> mybins_t;
    mybins_t myVerifyBins;
    QTimer myEditTimer;
};

#endif
