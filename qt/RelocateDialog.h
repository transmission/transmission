/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#pragma once

#include <QSet>

#include "BaseDialog.h"

#include "ui_RelocateDialog.h"

class Session;
class TorrentModel;

class RelocateDialog: public BaseDialog
{
    Q_OBJECT

  public:
    RelocateDialog (Session&, const TorrentModel&, const QSet<int>& ids, QWidget * parent = nullptr);
    virtual ~RelocateDialog () {}

  private:
    QString newLocation () const;

  private slots:
    void onSetLocation ();
    void onMoveToggled (bool);

  private:
    Session& mySession;
    const QSet<int> myIds;

    Ui::RelocateDialog ui;

    static bool myMoveFlag;
};

