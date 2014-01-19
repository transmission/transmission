/*
 * This file Copyright (C) 2009-2014 Mnemosyne LLC
 *
 * It may be used under the GNU Public License v2 or v3 licenses,
 * or any future license endorsed by Mnemosyne LLC.
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
    void onFileSelected (const QString& path);
    void onDirButtonClicked ();
    void onSetLocation ();
    void onMoveToggled (bool);

  public:
    RelocateDialog (Session&, TorrentModel&, const QSet<int>& ids, QWidget * parent = 0);
    ~RelocateDialog () {}
};

#endif
