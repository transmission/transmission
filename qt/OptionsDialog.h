/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef QTR_OPTIONS_DIALOG_H
#define QTR_OPTIONS_DIALOG_H

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QMap>
#include <QString>
#include <QTimer>
#include <QVector>

#include "AddData.h" // AddData
#include "BaseDialog.h"
#include "Torrent.h" // FileList

#include "ui_OptionsDialog.h"

class Prefs;
class Session;

extern "C"
{
  struct tr_variant;
}

class OptionsDialog: public BaseDialog
{
    Q_OBJECT

  public:
    OptionsDialog (Session& session, const Prefs& prefs, const AddData& addme, QWidget * parent = nullptr);
    virtual ~OptionsDialog ();

  private:
    typedef QMap<uint32_t, int32_t> mybins_t;

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

    void onSourceChanged ();
    void onDestinationChanged ();

  private:
    Session& mySession;
    AddData myAdd;

    Ui::OptionsDialog ui;

    QDir myLocalDestination;
    bool myHaveInfo;
    tr_info myInfo;
    QPushButton * myVerifyButton;
    QVector<int> myPriorities;
    QVector<bool> myWanted;
    FileList myFiles;

    QTimer myVerifyTimer;
    char myVerifyBuf[2048 * 4];
    QFile myVerifyFile;
    uint64_t myVerifyFilePos;
    int myVerifyFileIndex;
    uint32_t myVerifyPieceIndex;
    uint32_t myVerifyPiecePos;
    QVector<bool> myVerifyFlags;
    QCryptographicHash myVerifyHash;
    mybins_t myVerifyBins;
    QTimer myEditTimer;
};

#endif // QTR_OPTIONS_DIALOG_H
