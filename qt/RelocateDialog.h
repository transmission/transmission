/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <QSet>

#include "BaseDialog.h"

#include "ui_RelocateDialog.h"

class Session;
class TorrentModel;

class RelocateDialog : public BaseDialog
{
    Q_OBJECT

public:
    RelocateDialog(Session&, TorrentModel const&, QSet<int> const& ids, QWidget* parent = nullptr);

    virtual ~RelocateDialog()
    {
    }

private:
    QString newLocation() const;

private slots:
    void onSetLocation();
    void onMoveToggled(bool);

private:
    Session& mySession;
    QSet<int> const myIds;

    Ui::RelocateDialog ui;

    static bool myMoveFlag;
};
