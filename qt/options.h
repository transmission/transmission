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

#include <QDialog>
#include <QEvent>
#include <QString>
#include <QDir>
#include <QVector>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QCryptographicHash>
#include <QFile>
#include <QTimer>
#include <QLineEdit>

#include "add-data.h" // AddData
#include "file-tree.h" // FileList

class QCheckBox;
class QComboBox;
class QPushButton;

class FileTreeView;
class FreespaceLabel;
class Prefs;
class Session;

extern "C"
{
  struct tr_variant;
}

class Options: public QDialog
{
    Q_OBJECT

  public:
    Options (Session& session, const Prefs& prefs, const AddData& addme, QWidget * parent = 0);
    ~Options ();

  private:
    void reload ();
    void clearInfo ();
    void refreshSource (int width=-1);
    void refreshDestinationButton (int width=-1);
    void refreshButton (QPushButton *, const QString&, int width=-1);
    bool eventFilter (QObject *, QEvent *);

  private slots:
    void onAccepted ();
    void onPriorityChanged (const QSet<int>& fileIndices, int);
    void onWantedChanged (const QSet<int>& fileIndices, bool);
    void onVerify ();
    void onTimeout ();
    void onFilenameClicked ();
    void onDestinationClicked ();
    void onFilesSelected (const QStringList&);
    void onSourceEditingFinished ();
    void onDestinationsSelected (const QStringList&);
    void onDestinationEdited (const QString&);
    void onDestinationEditedIdle ();

  private:
    Session& mySession;
    AddData myAdd;
    QDir myLocalDestination;
    bool myHaveInfo;
    tr_info myInfo;
    FileTreeView * myTree;
    FreespaceLabel * myFreespaceLabel;
    QCheckBox * myStartCheck;
    QCheckBox * myTrashCheck;
    QComboBox * myPriorityCombo;
    QPushButton * mySourceButton;
    QLineEdit * mySourceEdit;
    QPushButton * myDestinationButton;
    QLineEdit * myDestinationEdit;
    QPushButton * myVerifyButton;
    QVector<int> myPriorities;
    QVector<bool> myWanted;
    FileList myFiles;

  private:
    QTimer myVerifyTimer;
    char myVerifyBuf[2048*4];
    QFile myVerifyFile;
    uint64_t myVerifyFilePos;
    int myVerifyFileIndex;
    uint32_t myVerifyPieceIndex;
    uint32_t myVerifyPiecePos;
    void clearVerify ();
    QVector<bool> myVerifyFlags;
    QCryptographicHash myVerifyHash;
    typedef QMap<uint32_t,int32_t> mybins_t;
    mybins_t myVerifyBins;
    QTimer myEditTimer;
};

#endif
