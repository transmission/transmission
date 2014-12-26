/*
 * This file Copyright (C) 2009-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef RELOCATE_DIALOG_H
#define RELOCATE_DIALOG_H

#include <QDialog>
#include <QSet>
#include <QString>

#include "ui_relocate.h"

class Session;
class TorrentModel;

class RelocateDialog: public QDialog
{
    Q_OBJECT

  public:
    RelocateDialog (Session&, const TorrentModel&, const QSet<int>& ids, QWidget * parent = 0);
    ~RelocateDialog () {}

  private slots:
    void onFileSelected (const QString& path);
    void onDirButtonClicked ();
    void onSetLocation ();
    void onMoveToggled (bool);

  private:
    Session& mySession;
    const QSet<int> myIds;
    Ui::RelocateDialog ui;
    QString myPath;

    static bool myMoveFlag;
};

#endif
