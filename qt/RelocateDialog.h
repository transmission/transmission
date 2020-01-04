/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include "BaseDialog.h"
#include "Typedefs.h"

#include "ui_RelocateDialog.h"

class Session;
class TorrentModel;

class RelocateDialog : public BaseDialog
{
    Q_OBJECT

public:
    RelocateDialog(Session&, TorrentModel const&, torrent_ids_t const& ids, QWidget* parent = nullptr);

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
    torrent_ids_t const myIds;

    Ui::RelocateDialog ui;

    static bool myMoveFlag;
};
